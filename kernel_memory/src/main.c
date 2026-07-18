#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <semaphore.h>
#include <commons/log.h>
#include <commons/config.h>
#include <commons/collections/list.h>
#include <utils/sockets.h>
#include <utils/protocolo.h>

// ---------------------------------------------------------------------------
// Estructuras internas
// ---------------------------------------------------------------------------

// Un Memory Stick conectado
typedef struct {
    uint32_t id;
    int      fd;
    uint32_t tamanio;
    uint32_t offset_global; // dónde empieza en el espacio de memoria total
} t_memory_stick;

// Un hueco libre en memoria
typedef struct {
    uint32_t base_global;   // dirección global (suma de todos los MS previos)
    uint32_t tamanio;
} t_hueco;

// Metadata de bloques Swap por segmento suspendido
typedef struct {
    uint32_t  pid;
    uint32_t  id_segmento;
    uint32_t* bloques;      // array de números de bloque en Swap
    uint32_t  cant_bloques;
} t_swap_metadata;

// ---------------------------------------------------------------------------
// Globales
// ---------------------------------------------------------------------------
t_log*    logger;
t_config* config;

pthread_mutex_t mutex_contextos   = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_memoria     = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_swap        = PTHREAD_MUTEX_INITIALIZER;

t_list* contextos;      // t_contexto*
t_list* memory_sticks;  // t_memory_stick*
t_list* huecos;         // t_hueco*
t_list* swap_metadata;  // t_swap_metadata*

typedef struct { uint32_t pid; char* path; } t_pid_path;
static t_list* script_paths = NULL; // t_pid_path* — protegido por mutex_contextos

int     fd_ks   = -1;   // socket del Kernel Scheduler
int     fd_swap = -1;   // socket del Swap
uint32_t swap_block_size   = 0;
uint32_t swap_cant_bloques = 0;
uint8_t* swap_bitmap       = NULL; // 0=libre, 1=ocupado

uint32_t next_ms_id = 0;

// NOTA [fix deadlock compactación]: la versión anterior sincronizaba con un
// semáforo: el handler de MSG_CREAR_SEGMENTO se bloqueaba en sem_wait esperando
// que el de MSG_FIN_COMPACTACION hiciera sem_post. Pero ambos mensajes llegan
// por la MISMA conexión (la del KS), atendida por UN solo hilo: al bloquearse
// en sem_wait, nadie podía leer el MSG_FIN_COMPACTACION → deadlock determinístico
// en cada compactación. Ahora el pedido de CREAR_SEGMENTO que necesita compactar
// se guarda acá como "pendiente" y el hilo sigue leyendo la conexión; al llegar
// MSG_FIN_COMPACTACION se compacta, se reintenta el pedido y se lo responde.
// A lo sumo hay un pendiente: el KS serializa sus pedidos (mutex_km_req).
static int      compact_pend_activo  = 0;
static uint32_t compact_pend_pid     = 0;
static uint32_t compact_pend_id_seg  = 0;
static uint32_t compact_pend_tamanio = 0;

// ---------------------------------------------------------------------------
// Helpers — contextos
// ---------------------------------------------------------------------------
static t_contexto* buscar_contexto(uint32_t pid) {
    for (int i = 0; i < list_size(contextos); i++) {
        t_contexto* ctx = list_get(contextos, i);
        if (ctx->pid == pid) return ctx;
    }
    return NULL;
}

// ---------------------------------------------------------------------------
// Helpers — Memory Sticks
// ---------------------------------------------------------------------------

// Dirección global → MS y dirección local dentro de ese MS
static t_memory_stick* ms_para_direccion(uint32_t dir_global, uint32_t* dir_local) {
    for (int i = 0; i < list_size(memory_sticks); i++) {
        t_memory_stick* ms = list_get(memory_sticks, i);
        if (dir_global >= ms->offset_global &&
            dir_global <  ms->offset_global + ms->tamanio) {
            *dir_local = dir_global - ms->offset_global;
            return ms;
        }
    }
    return NULL;
}

static uint32_t memoria_total(void) {
    uint32_t total = 0;
    for (int i = 0; i < list_size(memory_sticks); i++) {
        t_memory_stick* ms = list_get(memory_sticks, i);
        total += ms->tamanio;
    }
    return total;
}

// ---------------------------------------------------------------------------
// Helpers — huecos
// ---------------------------------------------------------------------------
static void agregar_hueco(uint32_t base, uint32_t tamanio) {
    t_hueco* h = malloc(sizeof(t_hueco));
    h->base_global = base;
    h->tamanio     = tamanio;
    list_add(huecos, h);
}

// Fusionar huecos adyacentes (llamar con mutex_memoria tomado)
static void fusionar_huecos(void) {
    int cambio = 1;
    while (cambio) {
        cambio = 0;
        for (int i = 0; i < list_size(huecos); i++) {
            t_hueco* a = list_get(huecos, i);
            for (int j = 0; j < list_size(huecos); j++) {
                if (i == j) continue;
                t_hueco* b = list_get(huecos, j);
                if (a->base_global + a->tamanio == b->base_global) {
                    a->tamanio += b->tamanio;
                    list_remove(huecos, j);
                    free(b);
                    cambio = 1;
                    break;
                }
            }
            if (cambio) break;
        }
    }
}

// Best Fit: hueco más pequeño que alcance
static t_hueco* best_fit(uint32_t tamanio) {
    t_hueco* mejor = NULL;
    for (int i = 0; i < list_size(huecos); i++) {
        t_hueco* h = list_get(huecos, i);
        if (h->tamanio >= tamanio) {
            if (mejor == NULL || h->tamanio < mejor->tamanio)
                mejor = h;
        }
    }
    return mejor;
}

// Worst Fit: hueco más grande
static t_hueco* worst_fit(uint32_t tamanio) {
    t_hueco* peor = NULL;
    for (int i = 0; i < list_size(huecos); i++) {
        t_hueco* h = list_get(huecos, i);
        if (h->tamanio >= tamanio) {
            if (peor == NULL || h->tamanio > peor->tamanio)
                peor = h;
        }
    }
    return peor;
}

static t_hueco* seleccionar_hueco(uint32_t tamanio) {
    char* estrategia = config_get_string_value(config, "ALLOCATION_STRATEGY");
    if (strcmp(estrategia, "WORST") == 0)
        return worst_fit(tamanio);
    return best_fit(tamanio); // default BEST
}

// ---------------------------------------------------------------------------
// Helpers — comunicación con Memory Sticks
// ---------------------------------------------------------------------------

static int ms_escribir(t_memory_stick* ms, uint32_t dir_local,
                        uint8_t* datos, uint32_t tamanio) {
    // Payload: [dir_local:4][tamanio:4][datos]
    uint32_t psize = 8 + tamanio;
    uint8_t* buf   = malloc(psize);
    uint32_t dl_n  = htonl(dir_local);
    uint32_t ts_n  = htonl(tamanio);
    memcpy(buf,     &dl_n, 4);
    memcpy(buf + 4, &ts_n, 4);
    memcpy(buf + 8, datos, tamanio);
    enviar_mensaje(ms->fd, MSG_MEMORY_WRITE, buf, psize);
    free(buf);

    t_mensaje* resp = recibir_mensaje(ms->fd);
    int ok = (resp && resp->op_code == MSG_OK);
    if (resp) free_mensaje(resp);
    return ok ? 0 : -1;
}

static uint8_t* ms_leer(t_memory_stick* ms, uint32_t dir_local, uint32_t tamanio) {
    uint8_t buf[8];
    uint32_t dl_n = htonl(dir_local);
    uint32_t ts_n = htonl(tamanio);
    memcpy(buf,     &dl_n, 4);
    memcpy(buf + 4, &ts_n, 4);
    enviar_mensaje(ms->fd, MSG_MEMORY_READ, buf, 8);

    t_mensaje* resp = recibir_mensaje(ms->fd);
    if (!resp || resp->op_code != MSG_MEMORY_READ_RESPUESTA) {
        if (resp) free_mensaje(resp);
        return NULL;
    }
    uint8_t* datos = malloc(tamanio);
    memcpy(datos, resp->payload, tamanio);
    free_mensaje(resp);
    return datos;
}

// Leer/escribir abarcando múltiples Memory Sticks
static uint8_t* leer_fisico(uint32_t dir_global, uint32_t tamanio) {
    uint8_t* resultado = malloc(tamanio);
    uint32_t leido = 0;
    while (leido < tamanio) {
        uint32_t dir_local;
        t_memory_stick* ms = ms_para_direccion(dir_global + leido, &dir_local);
        if (!ms) { free(resultado); return NULL; }

        uint32_t disponible = ms->tamanio - dir_local;
        uint32_t a_leer     = (tamanio - leido < disponible) ? tamanio - leido : disponible;

        uint8_t* chunk = ms_leer(ms, dir_local, a_leer);
        if (!chunk) { free(resultado); return NULL; }
        memcpy(resultado + leido, chunk, a_leer);
        free(chunk);
        leido += a_leer;
    }
    return resultado;
}

static int escribir_fisico(uint32_t dir_global, uint8_t* datos, uint32_t tamanio) {
    uint32_t escrito = 0;
    while (escrito < tamanio) {
        uint32_t dir_local;
        t_memory_stick* ms = ms_para_direccion(dir_global + escrito, &dir_local);
        if (!ms) return -1;

        uint32_t disponible = ms->tamanio - dir_local;
        uint32_t a_escribir = (tamanio - escrito < disponible) ? tamanio - escrito : disponible;

        if (ms_escribir(ms, dir_local, datos + escrito, a_escribir) < 0) return -1;
        escrito += a_escribir;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Traducción de dirección lógica → física
// Retorna la dirección global, o UINT32_MAX si hay segfault
// ---------------------------------------------------------------------------
static uint32_t traducir_direccion(t_contexto* ctx, uint32_t dir_logica,
                                    uint32_t tamanio_acceso) {
    uint32_t seg_max = (uint32_t)config_get_int_value(config, "SEGMENT_MAX_SIZE");
    uint32_t num_seg      = dir_logica / seg_max;
    uint32_t desplazamiento = dir_logica % seg_max;

    if (num_seg >= ctx->cant_segmentos) {
        log_debug(logger,
            "PID: %u - traducir_direccion: segmento %u inexistente (proceso tiene %u) — dir_logica=%u",
            ctx->pid, num_seg, ctx->cant_segmentos, dir_logica);
        return UINT32_MAX;
    }

    t_entrada_segmento* seg = &ctx->segmentos[num_seg];
    if (desplazamiento + tamanio_acceso > seg->limite) {
        log_debug(logger,
            "PID: %u - traducir_direccion: SEG_FAULT en segmento %u — desplazamiento=%u + tamanio=%u > limite=%u",
            ctx->pid, num_seg, desplazamiento, tamanio_acceso, seg->limite);
        return UINT32_MAX;
    }

    return seg->base + desplazamiento;
}

// ---------------------------------------------------------------------------
// Helpers — Swap
// ---------------------------------------------------------------------------
static int swap_bloque_libre(void) {
    for (uint32_t i = 0; i < swap_cant_bloques; i++)
        if (swap_bitmap[i] == 0) return (int)i;
    return -1;
}

static int swap_leer_bloque(uint32_t nro_bloque, uint8_t* out) {
    pthread_mutex_lock(&mutex_swap);
    uint8_t buf[4];
    uint32_t nb_n = htonl(nro_bloque);
    memcpy(buf, &nb_n, 4);
    enviar_mensaje(fd_swap, MSG_SWAP_LEER, buf, 4);
    t_mensaje* resp = recibir_mensaje(fd_swap);
    int ok = 0;
    if (resp && resp->op_code == MSG_SWAP_LEER_RESP) {
        memcpy(out, resp->payload, swap_block_size);
        ok = 1;
    }
    if (resp) free_mensaje(resp);
    pthread_mutex_unlock(&mutex_swap);
    return ok ? 0 : -1;
}

static int swap_escribir_bloque(uint32_t nro_bloque, uint8_t* datos) {
    pthread_mutex_lock(&mutex_swap);
    uint32_t psize = 4 + swap_block_size;
    uint8_t* buf   = malloc(psize);
    uint32_t nb_n  = htonl(nro_bloque);
    memcpy(buf, &nb_n, 4);
    memcpy(buf + 4, datos, swap_block_size);
    enviar_mensaje(fd_swap, MSG_SWAP_ESCRIBIR, buf, psize);
    free(buf);
    t_mensaje* resp = recibir_mensaje(fd_swap);
    int ok = (resp && resp->op_code == MSG_OK);
    if (resp) free_mensaje(resp);
    pthread_mutex_unlock(&mutex_swap);
    return ok ? 0 : -1;
}

// ---------------------------------------------------------------------------
// Creación de segmento (con Best/Worst Fit)
// Retorna 1 si hay que compactar, 0 si OK, -1 si error
// ---------------------------------------------------------------------------
static int crear_segmento(t_contexto* ctx, uint32_t id_seg, uint32_t tamanio) {
    uint32_t seg_max = (uint32_t)config_get_int_value(config, "SEGMENT_MAX_SIZE");
    if (tamanio > seg_max) return -1;

    pthread_mutex_lock(&mutex_memoria);
    t_hueco* h = seleccionar_hueco(tamanio);
    if (h == NULL) {
        // Verificar si la memoria total libre alcanza (fragmentada)
        uint32_t libre_total = 0;
        for (int i = 0; i < list_size(huecos); i++) {
            t_hueco* hi = list_get(huecos, i);
            libre_total += hi->tamanio;
        }
        pthread_mutex_unlock(&mutex_memoria);
        log_debug(logger,
            "PID: %u - crear_segmento %u (%u bytes): sin hueco contiguo — libre_total=%u -> %s",
            ctx->pid, id_seg, tamanio, libre_total,
            (libre_total >= tamanio) ? "pide compactación" : "sin espacio total, error");
        return (libre_total >= tamanio) ? 1 : -1; // 1 = necesita compactación
    }
    log_debug(logger,
        "PID: %u - crear_segmento %u (%u bytes): hueco elegido (%s) base=%u tamanio_hueco=%u",
        ctx->pid, id_seg, tamanio, config_get_string_value(config, "ALLOCATION_STRATEGY"),
        h->base_global, h->tamanio);

    // Asignar en el hueco
    uint32_t base = h->base_global;

    // Encontrar a qué MS pertenece esta dirección global
    uint32_t dir_local;
    t_memory_stick* ms = ms_para_direccion(base, &dir_local);
    if (!ms) { pthread_mutex_unlock(&mutex_memoria); return -1; }

    // Actualizar hueco
    if (h->tamanio == tamanio) {
        // NOTA LUCIANO [FIX list_get_index en crear_segmento]:
        // list_get_index() no existe en so-commons. La API correcta para
        // eliminar un elemento por puntero es list_remove_element(), que
        // lo busca por igualdad de puntero y lo saca de la lista sin destruirlo.
        // Llamamos free(h) manualmente porque list_remove_element no lo hace.
        list_remove_element(huecos, h);
        free(h);
    } else {
        h->base_global += tamanio;
        h->tamanio     -= tamanio;
    }

    // Agregar entrada a la tabla de segmentos del proceso
    ctx->cant_segmentos++;
    ctx->segmentos = realloc(ctx->segmentos,
                             ctx->cant_segmentos * sizeof(t_entrada_segmento));
    t_entrada_segmento* seg = &ctx->segmentos[ctx->cant_segmentos - 1];
    seg->id_segmento    = id_seg;
    seg->id_memory_stick = ms->id;
    seg->base           = base;
    seg->limite         = tamanio;

    pthread_mutex_unlock(&mutex_memoria);

    log_info(logger, "## PID: %u - Segmento Creado %u - Tamaño: %u",
             ctx->pid, id_seg, tamanio);
    return 0;
}

// ---------------------------------------------------------------------------
// Eliminación de segmento
// ---------------------------------------------------------------------------
static void eliminar_segmento(t_contexto* ctx, uint32_t id_seg) {
    pthread_mutex_lock(&mutex_memoria);
    for (uint32_t i = 0; i < ctx->cant_segmentos; i++) {
        if (ctx->segmentos[i].id_segmento == id_seg) {
            agregar_hueco(ctx->segmentos[i].base, ctx->segmentos[i].limite);
            fusionar_huecos();
            // Desplazar el resto del array
            for (uint32_t j = i; j < ctx->cant_segmentos - 1; j++)
                ctx->segmentos[j] = ctx->segmentos[j + 1];
            ctx->cant_segmentos--;
            ctx->segmentos = realloc(ctx->segmentos,
                                     ctx->cant_segmentos * sizeof(t_entrada_segmento));
            break;
        }
    }
    pthread_mutex_unlock(&mutex_memoria);
}

// ---------------------------------------------------------------------------
// Finalización de proceso — libera todo lo que tenía ocupado
// (segmentos residentes en memoria, bloques en Swap, contexto y script_path).
// Se llama cuando el KS avisa que un proceso terminó (EXIT/ERROR/SEG_FAULT);
// sin esto la memoria que tenía asignada queda perdida para siempre.
// ---------------------------------------------------------------------------
static void finalizar_proceso(uint32_t pid) {
    pthread_mutex_lock(&mutex_contextos);
    t_contexto* ctx = buscar_contexto(pid);
    if (!ctx) {
        pthread_mutex_unlock(&mutex_contextos);
        return;
    }

    // Segmentos todavía residentes en memoria física.
    while (ctx->cant_segmentos > 0) {
        eliminar_segmento(ctx, ctx->segmentos[0].id_segmento);
    }

    // Segmentos que quedaron en Swap (proceso suspendido al momento de finalizar).
    for (int i = list_size(swap_metadata) - 1; i >= 0; i--) {
        t_swap_metadata* m = list_get(swap_metadata, i);
        if (m->pid != pid) continue;
        for (uint32_t b = 0; b < m->cant_bloques; b++) {
            swap_bitmap[m->bloques[b]] = 0;
        }
        list_remove(swap_metadata, i);
        free(m->bloques);
        free(m);
    }

    list_remove_element(contextos, ctx);
    free(ctx->segmentos);
    free(ctx);

    for (int i = 0; i < list_size(script_paths); i++) {
        t_pid_path* pp = list_get(script_paths, i);
        if (pp->pid == pid) {
            list_remove(script_paths, i);
            free(pp->path);
            free(pp);
            break;
        }
    }

    pthread_mutex_unlock(&mutex_contextos);
    log_info(logger, "## PID: %u - Proceso finalizado, memoria liberada", pid);
}

// ---------------------------------------------------------------------------
// Compactación
// ---------------------------------------------------------------------------
static void compactar(void) {
    pthread_mutex_lock(&mutex_memoria);

    uint32_t cursor = 0;

    // Recorrer todos los contextos y mover cada segmento al inicio
    for (int c = 0; c < list_size(contextos); c++) {
        t_contexto* ctx = list_get(contextos, c);
        for (uint32_t s = 0; s < ctx->cant_segmentos; s++) {
            t_entrada_segmento* seg = &ctx->segmentos[s];
            if (seg->base != cursor) {
                // Leer datos actuales
                uint8_t* datos = leer_fisico(seg->base, seg->limite);
                if (datos) {
                    escribir_fisico(cursor, datos, seg->limite);
                    free(datos);
                }
                seg->base = cursor;
                // Actualizar id_memory_stick
                uint32_t dl;
                t_memory_stick* ms = ms_para_direccion(cursor, &dl);
                if (ms) seg->id_memory_stick = ms->id;
            }
            cursor += seg->limite;
        }
    }

    // Reconstruir lista de huecos: un único hueco al final
    list_destroy_and_destroy_elements(huecos, free);
    huecos = list_create();
    uint32_t total = memoria_total();
    if (cursor < total)
        agregar_hueco(cursor, total - cursor);

    pthread_mutex_unlock(&mutex_memoria);

    int delay_ms = config_get_int_value(config, "COMPACTION_DELAY");
    usleep(delay_ms * 1000);
}

// ---------------------------------------------------------------------------
// Suspensión — mover segmentos a Swap
// ---------------------------------------------------------------------------
static int suspender_proceso(uint32_t pid) {
    pthread_mutex_lock(&mutex_contextos);
    t_contexto* ctx = buscar_contexto(pid);
    if (!ctx) { pthread_mutex_unlock(&mutex_contextos); return -1; }

    log_debug(logger, "PID: %u - suspender_proceso: moviendo %u segmento(s) a Swap",
              pid, ctx->cant_segmentos);

    for (uint32_t s = 0; s < ctx->cant_segmentos; s++) {
        t_entrada_segmento* seg = &ctx->segmentos[s];
        uint32_t tamanio = seg->limite;

        // Cuántos bloques necesita
        uint32_t cant = (tamanio + swap_block_size - 1) / swap_block_size;
        log_debug(logger, "PID: %u - segmento %u (%u bytes) -> %u bloque(s) de swap",
                  pid, seg->id_segmento, tamanio, cant);

        t_swap_metadata* meta = malloc(sizeof(t_swap_metadata));
        meta->pid         = pid;
        meta->id_segmento = seg->id_segmento;
        meta->cant_bloques = cant;
        meta->bloques     = malloc(cant * sizeof(uint32_t));

        uint8_t* datos = leer_fisico(seg->base, tamanio);

        for (uint32_t b = 0; b < cant; b++) {
            int nb = swap_bloque_libre();
            if (nb < 0) {
                free(meta->bloques); free(meta); free(datos);
                pthread_mutex_unlock(&mutex_contextos);
                return -1;
            }
            swap_bitmap[nb] = 1;
            meta->bloques[b] = (uint32_t)nb;

            // Datos del bloque (puede ser parcial en el último)
            uint8_t bloque_buf[swap_block_size];
            memset(bloque_buf, 0, swap_block_size);
            uint32_t offset = b * swap_block_size;
            uint32_t chunk  = (tamanio - offset < swap_block_size)
                              ? tamanio - offset : swap_block_size;
            if (datos) memcpy(bloque_buf, datos + offset, chunk);
            swap_escribir_bloque((uint32_t)nb, bloque_buf);
        }
        if (datos) free(datos);

        pthread_mutex_lock(&mutex_memoria);
        agregar_hueco(seg->base, seg->limite);
        fusionar_huecos();
        pthread_mutex_unlock(&mutex_memoria);

        list_add(swap_metadata, meta);
    }

    // Limpiar tabla de segmentos (quedan en swap_metadata)
    free(ctx->segmentos);
    ctx->segmentos      = NULL;
    ctx->cant_segmentos = 0;

    pthread_mutex_unlock(&mutex_contextos);
    return 0;
}

// ---------------------------------------------------------------------------
// Des-suspensión — restaurar segmentos desde Swap
// Retorna 0=ok, 1=no cabe sin compactar, -1=error
// ---------------------------------------------------------------------------
static int dessuspender_proceso(uint32_t pid) {
    pthread_mutex_lock(&mutex_contextos);
    t_contexto* ctx = buscar_contexto(pid);
    if (!ctx) { pthread_mutex_unlock(&mutex_contextos); return -1; }

    // Recolectar metadata de Swap para este PID
    t_list* metas_pid = list_create();
    for (int i = 0; i < list_size(swap_metadata); i++) {
        t_swap_metadata* m = list_get(swap_metadata, i);
        if (m->pid == pid) list_add(metas_pid, m);
    }
    log_debug(logger, "PID: %u - dessuspender_proceso: %d segmento(s) a restaurar desde Swap",
              pid, list_size(metas_pid));

    // Verificar que todos los segmentos caben sin compactar
    pthread_mutex_lock(&mutex_memoria);
    for (int i = 0; i < list_size(metas_pid); i++) {
        t_swap_metadata* m = list_get(metas_pid, i);
        uint32_t tamanio = m->cant_bloques * swap_block_size;
        t_hueco* h = seleccionar_hueco(tamanio);
        if (!h) {
            pthread_mutex_unlock(&mutex_memoria);
            pthread_mutex_unlock(&mutex_contextos);
            list_destroy(metas_pid);
            return 1; // no cabe sin compactar
        }
    }
    pthread_mutex_unlock(&mutex_memoria);

    // Restaurar cada segmento
    for (int i = 0; i < list_size(metas_pid); i++) {
        t_swap_metadata* m = list_get(metas_pid, i);
        uint32_t tamanio   = m->cant_bloques * swap_block_size;

        pthread_mutex_lock(&mutex_memoria);
        t_hueco* h = seleccionar_hueco(tamanio);
        uint32_t base = h->base_global;
        if (h->tamanio == tamanio) {
            // NOTA LUCIANO [FIX list_get_index en dessuspender_proceso]:
            // Mismo problema que en crear_segmento: list_get_index() no existe.
            // Ver comentario en crear_segmento para la explicación completa.
            list_remove_element(huecos, h);
            free(h);
        } else {
            h->base_global += tamanio;
            h->tamanio     -= tamanio;
        }
        pthread_mutex_unlock(&mutex_memoria);

        // Leer bloques de Swap y escribir en memoria
        uint8_t* datos = malloc(tamanio);
        for (uint32_t b = 0; b < m->cant_bloques; b++) {
            swap_leer_bloque(m->bloques[b], datos + b * swap_block_size);
            swap_bitmap[m->bloques[b]] = 0; // liberar bloque
        }
        escribir_fisico(base, datos, tamanio);
        free(datos);

        // Agregar a la tabla de segmentos del proceso
        uint32_t dl;
        t_memory_stick* ms = ms_para_direccion(base, &dl);
        ctx->cant_segmentos++;
        ctx->segmentos = realloc(ctx->segmentos,
                                 ctx->cant_segmentos * sizeof(t_entrada_segmento));
        t_entrada_segmento* seg = &ctx->segmentos[ctx->cant_segmentos - 1];
        seg->id_segmento     = m->id_segmento;
        seg->id_memory_stick = ms ? ms->id : 0;
        seg->base            = base;
        seg->limite          = tamanio;

        // Eliminar de la lista global de swap_metadata
        for (int j = 0; j < list_size(swap_metadata); j++) {
            if (list_get(swap_metadata, j) == m) {
                list_remove(swap_metadata, j);
                free(m->bloques);
                free(m);
                break;
            }
        }
    }

    list_destroy(metas_pid);
    pthread_mutex_unlock(&mutex_contextos);
    return 0;
}

// ---------------------------------------------------------------------------
// NOTA LUCIANO [FIX leer_instruccion — forward declaration]:
// leer_instruccion() está definida más abajo en el archivo (después de main)
// pero se usa dentro de atender_cliente(). En C, una función estática debe
// declararse antes de su primer uso; sin esto, el compilador asume que
// devuelve int y lanza un error de "conflicting types" al encontrar la
// definición real. La solución es agregar esta declaración adelantada.
static char* leer_instruccion(uint32_t pid, uint32_t pc);

// Hilo por cliente
// ---------------------------------------------------------------------------
typedef struct { int fd; } t_args;

static void* atender_cliente(void* arg) {
    t_args* a = (t_args*)arg;
    int fd = a->fd;
    free(a);

    t_mensaje* msg = recibir_mensaje(fd);
    if (!msg) return NULL;

    int identificado = 1;
    int conexion_pasiva = 0; // 1 = MS/Swap: este hilo deja de leer el fd tras identificar

    switch (msg->op_code) {

        case MSG_KS_IDENTIFICACION:
            log_info(logger, "## Kernel Scheduler Conectado - FD del socket: %d", fd);
            fd_ks = fd;
            enviar_mensaje(fd, MSG_OK, NULL, 0);
            break;

        case MSG_MEMORY_STICK_IDENTIFICACION: {
            if (msg->payload_size >= 4) {
                uint32_t tn; memcpy(&tn, msg->payload, 4);
                uint32_t tamanio = ntohl(tn);
                log_info(logger, "## Memory Stick de %u bytes Conectada", tamanio);

                pthread_mutex_lock(&mutex_memoria);
                t_memory_stick* ms = malloc(sizeof(t_memory_stick));
                ms->id           = next_ms_id++;
                ms->fd           = fd;
                ms->tamanio      = tamanio;
                ms->offset_global = memoria_total();
                list_add(memory_sticks, ms);
                agregar_hueco(ms->offset_global, tamanio);
                pthread_mutex_unlock(&mutex_memoria);

                // El stick necesita saber dónde empieza dentro del espacio
                // global de direcciones para poder traducir las direcciones
                // globales que le llegan directo de la CPU (MOV_IN/MOV_OUT/
                // COPY_MEM) a un offset local a su propio buffer.
                uint32_t offset_n = htonl(ms->offset_global);
                enviar_mensaje(fd, MSG_OK, &offset_n, sizeof(offset_n));

                // Notificar al KS que hay más memoria
                if (fd_ks >= 0)
                    enviar_mensaje(fd_ks, MSG_MAS_MEMORIA, NULL, 0);

                conexion_pasiva = 1;
            } else {
                log_warning(logger, "Memory Stick sin payload (fd=%d)", fd);
                enviar_mensaje(fd, MSG_ERROR, NULL, 0);
                identificado = 0;
            }
            break;
        }

        case MSG_SWAP_IDENTIFICACION: {
            if (msg->payload_size >= 8) {
                uint32_t ss_n, bs_n;
                memcpy(&ss_n, msg->payload,     4);
                memcpy(&bs_n, (uint8_t*)msg->payload + 4, 4);
                uint32_t swap_size  = ntohl(ss_n);
                uint32_t block_size = ntohl(bs_n);
                swap_block_size   = block_size;
                swap_cant_bloques = swap_size / block_size;
                swap_bitmap = calloc(swap_cant_bloques, 1);
                fd_swap = fd;
                log_info(logger,
                    "## Swap Conectado - FD: %d - Tamaño: %u bytes - Bloque: %u bytes - Bloques totales: %u",
                    fd, swap_size, block_size, swap_cant_bloques);
                enviar_mensaje(fd, MSG_OK, NULL, 0);
                conexion_pasiva = 1;
            } else {
                enviar_mensaje(fd, MSG_ERROR, NULL, 0);
                identificado = 0;
            }
            break;
        }

        case MSG_CPU_A_KERNEL_MEMORY:
        case MSG_CPU_IDENTIFICACION: {
            if (msg->payload_size >= 4) {
                uint32_t id_n; memcpy(&id_n, msg->payload, 4);
                log_info(logger, "## CPU %u Conectada", ntohl(id_n));
            } else {
                log_info(logger, "## CPU Conectada - FD: %d", fd);
            }
            enviar_mensaje(fd, MSG_OK, NULL, 0);
            break;
        }

        default:
            log_warning(logger, "op_code de identificación desconocido: %d", msg->op_code);
            enviar_mensaje(fd, MSG_ERROR, NULL, 0);
            identificado = 0;
            break;
    }

    free_mensaje(msg);
    if (!identificado) return NULL;

    // NOTA [fix lectura concurrente MS/Swap]: los fds de Memory Stick y Swap se
    // usan en modo pedido/respuesta desde los helpers (ms_leer, ms_escribir,
    // swap_leer_bloque, swap_escribir_bloque). Si este hilo siguiera leyendo la
    // conexión, competiría con esos helpers por las respuestas (dos lectores del
    // mismo socket) y compactación/STDOUT/suspensión se colgarían esperando una
    // respuesta ya consumida. La conexión queda abierta; este hilo termina acá.
    if (conexion_pasiva) return NULL;

    // ------------------------------------------------------------------
    // Loop de atención
    // ------------------------------------------------------------------
    t_mensaje* pedido;
    while ((pedido = recibir_mensaje(fd)) != NULL) {

        // CREAR PROCESO
        if (pedido->op_code == MSG_CREAR_PROCESO) {
            if (pedido->payload_size < 5) {
                enviar_mensaje(fd, MSG_ERROR, NULL, 0);
                free_mensaje(pedido); continue;
            }
            uint32_t pid_n; memcpy(&pid_n, pedido->payload, 4);
            uint32_t pid = ntohl(pid_n);
            char* script_name = (char*)pedido->payload + 4; // null-terminated por protocolo

            pthread_mutex_lock(&mutex_contextos);
            if (buscar_contexto(pid)) {
                pthread_mutex_unlock(&mutex_contextos);
                enviar_mensaje(fd, MSG_ERROR, NULL, 0);
                free_mensaje(pedido); continue;
            }
            t_contexto* ctx = calloc(1, sizeof(t_contexto));
            ctx->pid = pid;
            list_add(contextos, ctx);
            t_pid_path* pp = malloc(sizeof(t_pid_path));
            pp->pid  = pid;
            pp->path = strdup(script_name);
            list_add(script_paths, pp);
            pthread_mutex_unlock(&mutex_contextos);

            log_info(logger, "## PID: %u - Proceso Creado", pid);
            log_debug(logger, "PID: %u - Path de script guardado: %s", pid, script_name);
            enviar_mensaje(fd, MSG_OK, NULL, 0);
        }

        // FETCH INSTRUCCIÓN
        else if (pedido->op_code == MSG_FETCH_INSTRUCCION) {
            t_fetch_request* req = deserializar_fetch_request(pedido->payload);
            int delay_ms = config_get_int_value(config, "INSTRUCTION_DELAY");
            usleep(delay_ms * 1000);
            char* inst = leer_instruccion(req->pid, req->pc);
            if (!inst) {
                enviar_mensaje(fd, MSG_ERROR, NULL, 0);
            } else {
                log_info(logger, "## PID: %u - Obtener instrucción: %u - Instrucción: %s",
                         req->pid, req->pc, inst);
                uint32_t sz; void* pl = serializar_string(inst, &sz);
                enviar_mensaje(fd, MSG_RESPUESTA_INSTRUCCION, pl, sz);
                free(pl); free(inst);
            }
            free(req);
        }

        // GUARDAR CONTEXTO
        else if (pedido->op_code == MSG_GUARDAR_CONTEXTO) {
            t_contexto* nuevo = deserializar_contexto(pedido->payload, pedido->payload_size);
            if (nuevo == NULL) {
                log_warning(logger, "MSG_GUARDAR_CONTEXTO con payload invalido");
                enviar_mensaje(fd, MSG_ERROR, NULL, 0);
                free_mensaje(pedido);
                continue;
            }

            pthread_mutex_lock(&mutex_contextos);
            t_contexto* ex = buscar_contexto(nuevo->pid);
            if (ex) {
                // Solo actualizamos los registros: KM es la fuente de verdad
                // para la tabla de segmentos (que puede haber sido modificada
                // por MEM_ALLOC/FREE mientras la CPU ejecutaba). Sobreescribir
                // los segmentos con la copia local de la CPU borraría segmentos
                // recién creados o restauraría segmentos ya liberados.
                ex->registros = nuevo->registros;
                enviar_mensaje(fd, MSG_OK, NULL, 0);
            } else {
                // NOTA [fix carrera EXIT/MUTEX_LOCK vs MSG_FINALIZAR_PROCESO]:
                // varias syscalls (EXIT, MUTEX_LOCK, etc.) son fire-and-forget
                // del lado de la CPU: manda el aviso a KS y sin esperar respuesta
                // sigue con su guardar_contexto_en_memory de fin de ciclo. Si KS
                // ya le pidió a KM que finalice el proceso (ver
                // MSG_FINALIZAR_PROCESO) para cuando este guardado llega, el
                // contexto ya no existe. No es un error real — el proceso ya
                // terminó y sus registros finales no tienen destino útil — así
                // que se responde OK en vez de ERROR. Antes de este fix, el
                // ERROR hacía que la CPU degradara un EXIT normal a motivo
                // ERROR y cortara su propio loop principal.
                log_debug(logger,
                    "PID: %u - MSG_GUARDAR_CONTEXTO llegó después de que el proceso ya fue finalizado — se ignora",
                    nuevo->pid);
                enviar_mensaje(fd, MSG_OK, NULL, 0);
            }
            pthread_mutex_unlock(&mutex_contextos);
            free_contexto(nuevo);
        }

        // RESTAURAR CONTEXTO
        else if (pedido->op_code == MSG_RESTAURAR_CONTEXTO) {
            uint32_t pid_n; memcpy(&pid_n, pedido->payload, 4);
            uint32_t pid = ntohl(pid_n);
            pthread_mutex_lock(&mutex_contextos);
            t_contexto* ctx = buscar_contexto(pid);
            if (ctx) {
                uint32_t sz; void* pl = serializar_contexto(ctx, &sz);
                pthread_mutex_unlock(&mutex_contextos);
                enviar_mensaje(fd, MSG_RESTAURAR_CONTEXTO, pl, sz);
                free(pl);
            } else {
                pthread_mutex_unlock(&mutex_contextos);
                enviar_mensaje(fd, MSG_ERROR, NULL, 0);
            }
        }

        // CREAR SEGMENTO
        else if (pedido->op_code == MSG_CREAR_SEGMENTO) {
            // Payload: [pid:4][id_seg:4][tamanio:4]
            uint32_t pid_n, id_n, tam_n;
            memcpy(&pid_n, pedido->payload,      4);
            memcpy(&id_n,  (uint8_t*)pedido->payload + 4, 4);
            memcpy(&tam_n, (uint8_t*)pedido->payload + 8, 4);
            uint32_t pid     = ntohl(pid_n);
            uint32_t id_seg  = ntohl(id_n);
            uint32_t tamanio = ntohl(tam_n);

            pthread_mutex_lock(&mutex_contextos);
            t_contexto* ctx = buscar_contexto(pid);
            if (!ctx) {
                pthread_mutex_unlock(&mutex_contextos);
                enviar_mensaje(fd, MSG_ERROR, NULL, 0);
                free_mensaje(pedido); continue;
            }

            int res = crear_segmento(ctx, id_seg, tamanio);
            pthread_mutex_unlock(&mutex_contextos);

            if (res == 1) {
                // NOTA [fix deadlock compactación]: este hilo es el ÚNICO que lee
                // la conexión del KS, así que no puede bloquearse esperando el
                // MSG_FIN_COMPACTACION (llega por esta misma conexión). Se avisa
                // al KS, se guarda el pedido como pendiente y se sigue atendiendo;
                // la respuesta al CREAR_SEGMENTO se envía desde el handler de
                // MSG_FIN_COMPACTACION, después de compactar y reintentar.
                compact_pend_activo  = 1;
                compact_pend_pid     = pid;
                compact_pend_id_seg  = id_seg;
                compact_pend_tamanio = tamanio;
                if (fd_ks >= 0) enviar_mensaje(fd_ks, MSG_COMPACTAR, NULL, 0);
                free_mensaje(pedido); continue;
            }

            if (res == 0) {
                // Devolver tabla de segmentos actualizada
                pthread_mutex_lock(&mutex_contextos);
                uint32_t sz; void* pl = serializar_contexto(ctx, &sz);
                pthread_mutex_unlock(&mutex_contextos);
                enviar_mensaje(fd, MSG_TABLA_SEGMENTOS, pl, sz);
                free(pl);
            } else {
                enviar_mensaje(fd, MSG_ERROR, NULL, 0);
            }
        }

        // ELIMINAR SEGMENTO
        else if (pedido->op_code == MSG_ELIMINAR_SEGMENTO) {
            uint32_t pid_n, id_n;
            memcpy(&pid_n, pedido->payload,      4);
            memcpy(&id_n,  (uint8_t*)pedido->payload + 4, 4);
            uint32_t pid    = ntohl(pid_n);
            uint32_t id_seg = ntohl(id_n);

            pthread_mutex_lock(&mutex_contextos);
            t_contexto* ctx = buscar_contexto(pid);
            if (ctx) eliminar_segmento(ctx, id_seg);
            pthread_mutex_unlock(&mutex_contextos);
            enviar_mensaje(fd, MSG_OK, NULL, 0);
        }

        // LEER DATOS (KS → KM → MS, para STDOUT)
        else if (pedido->op_code == MSG_LEER_DATOS) {
            uint32_t pid_n, dl_n, tam_n;
            memcpy(&pid_n, pedido->payload,      4);
            memcpy(&dl_n,  (uint8_t*)pedido->payload + 4, 4);
            memcpy(&tam_n, (uint8_t*)pedido->payload + 8, 4);
            uint32_t pid        = ntohl(pid_n);
            uint32_t dir_logica = ntohl(dl_n);
            uint32_t tamanio    = ntohl(tam_n);

            pthread_mutex_lock(&mutex_contextos);
            t_contexto* ctx = buscar_contexto(pid);
            if (!ctx) {
                pthread_mutex_unlock(&mutex_contextos);
                enviar_mensaje(fd, MSG_ERROR, NULL, 0);
                free_mensaje(pedido); continue;
            }
            uint32_t dir_fisica = traducir_direccion(ctx, dir_logica, tamanio);
            pthread_mutex_unlock(&mutex_contextos);

            if (dir_fisica == UINT32_MAX) {
                enviar_mensaje(fd, MSG_ERROR, NULL, 0);
            } else {
                log_info(logger, "## PID: %u - Lectura - Dir. Física: %u - Tamaño: %u",
                         pid, dir_fisica, tamanio);
                uint8_t* datos = leer_fisico(dir_fisica, tamanio);
                if (datos) {
                    enviar_mensaje(fd, MSG_LEER_DATOS_RESP, datos, tamanio);
                    free(datos);
                } else {
                    enviar_mensaje(fd, MSG_ERROR, NULL, 0);
                }
            }
        }

        // ESCRIBIR DATOS (KS → KM → MS, para STDIN)
        else if (pedido->op_code == MSG_ESCRIBIR_DATOS) {
            uint32_t pid_n, dl_n, tam_n;
            memcpy(&pid_n, pedido->payload,      4);
            memcpy(&dl_n,  (uint8_t*)pedido->payload + 4, 4);
            memcpy(&tam_n, (uint8_t*)pedido->payload + 8, 4);
            uint32_t pid        = ntohl(pid_n);
            uint32_t dir_logica = ntohl(dl_n);
            uint32_t tamanio    = ntohl(tam_n);
            uint8_t* datos      = (uint8_t*)pedido->payload + 12;

            pthread_mutex_lock(&mutex_contextos);
            t_contexto* ctx = buscar_contexto(pid);
            if (!ctx) {
                pthread_mutex_unlock(&mutex_contextos);
                enviar_mensaje(fd, MSG_ERROR, NULL, 0);
                free_mensaje(pedido); continue;
            }
            uint32_t dir_fisica = traducir_direccion(ctx, dir_logica, tamanio);
            pthread_mutex_unlock(&mutex_contextos);

            if (dir_fisica == UINT32_MAX) {
                enviar_mensaje(fd, MSG_ERROR, NULL, 0);
            } else {
                log_info(logger, "## PID: %u - Escritura - Dir. Física: %u - Tamaño: %u",
                         pid, dir_fisica, tamanio);
                int ok = escribir_fisico(dir_fisica, datos, tamanio);
                enviar_mensaje(fd, ok == 0 ? MSG_OK : MSG_ERROR, NULL, 0);
            }
        }

        // FIN COMPACTACIÓN (KS avisa que CPUs fueron desalojadas)
        else if (pedido->op_code == MSG_FIN_COMPACTACION) {
            // NOTA [fix deadlock compactación]: se compacta y se responde el
            // MSG_CREAR_SEGMENTO que quedó pendiente (ver arriba). No se envía
            // MSG_OK por el FIN: el KS no lo espera — se entera del fin de la
            // compactación al recibir la respuesta del CREAR_SEGMENTO, que por
            // esta misma conexión solo puede llegar después de compactar.
            log_info(logger, "## Inicio de compactación");
            compactar();
            log_info(logger, "## Fin de compactación");

            if (compact_pend_activo) {
                compact_pend_activo = 0;
                pthread_mutex_lock(&mutex_contextos);
                t_contexto* ctx_pend = buscar_contexto(compact_pend_pid);
                int res_pend = ctx_pend
                    ? crear_segmento(ctx_pend, compact_pend_id_seg, compact_pend_tamanio)
                    : -1;
                if (res_pend == 0) {
                    uint32_t sz; void* pl = serializar_contexto(ctx_pend, &sz);
                    pthread_mutex_unlock(&mutex_contextos);
                    enviar_mensaje(fd, MSG_TABLA_SEGMENTOS, pl, sz);
                    free(pl);
                } else {
                    pthread_mutex_unlock(&mutex_contextos);
                    enviar_mensaje(fd, MSG_ERROR, NULL, 0);
                }
            }
        }

        // SUSPENDER PROCESO
        else if (pedido->op_code == MSG_SUSPENDER_PROCESO) {
            uint32_t pid_n; memcpy(&pid_n, pedido->payload, 4);
            uint32_t pid = ntohl(pid_n);
            int res = suspender_proceso(pid);
            enviar_mensaje(fd, res == 0 ? MSG_OK : MSG_ERROR, NULL, 0);
        }

        // DES-SUSPENDER PROCESO
        else if (pedido->op_code == MSG_DESSUSPENDER_PROCESO) {
            uint32_t pid_n; memcpy(&pid_n, pedido->payload, 4);
            uint32_t pid = ntohl(pid_n);
            int res = dessuspender_proceso(pid);
            if (res == 0) {
                enviar_mensaje(fd, MSG_OK, NULL, 0);
            } else {
                // NOTA [fix protocolo des-suspensión]: si no cabe sin compactar
                // (res == 1) se responde MSG_ERROR, NO MSG_COMPACTAR: el enunciado
                // prohíbe que la des-suspensión dispare compactación, y además el
                // listener del KS interpretaría MSG_COMPACTAR como una orden de
                // desalojar CPUs mientras su km_request queda esperando respuesta.
                // El KS trata el error como "sin espacio" y deja el proceso
                // suspendido hasta el próximo MSG_MAS_MEMORIA.
                enviar_mensaje(fd, MSG_ERROR, NULL, 0);
            }
        }

        // FINALIZAR PROCESO (EXIT/ERROR/SEG_FAULT) — libera su memoria
        else if (pedido->op_code == MSG_FINALIZAR_PROCESO) {
            uint32_t pid_n; memcpy(&pid_n, pedido->payload, 4);
            uint32_t pid = ntohl(pid_n);
            finalizar_proceso(pid);
            enviar_mensaje(fd, MSG_OK, NULL, 0);
        }

        else {
            log_warning(logger, "op_code desconocido: %d", pedido->op_code);
            enviar_mensaje(fd, MSG_ERROR, NULL, 0);
        }

        free_mensaje(pedido);
    }

    log_info(logger, "Cliente desconectado (fd=%d)", fd);
    return NULL;
}

// ---------------------------------------------------------------------------
// Leer instrucción
// ---------------------------------------------------------------------------
static char* leer_instruccion(uint32_t pid, uint32_t pc) {
    char* base = config_get_string_value(config, "SCRIPTS_BASEPATH");
    char path[512];
    pthread_mutex_lock(&mutex_contextos);
    char* script_name = NULL;
    for (int i = 0; i < list_size(script_paths); i++) {
        t_pid_path* pp = list_get(script_paths, i);
        if (pp->pid == pid) { script_name = pp->path; break; }
    }
    if (script_name && script_name[0] == '/') snprintf(path, sizeof(path), "%s", script_name);
    else if (script_name)                     snprintf(path, sizeof(path), "%s/%s", base, script_name);
    else                                      snprintf(path, sizeof(path), "%s/%u.txt", base, pid);
    pthread_mutex_unlock(&mutex_contextos);
    FILE* f = fopen(path, "r");
    if (!f) { log_error(logger, "No se pudo abrir: %s", path); return NULL; }
    char line[256];
    uint32_t n = 0;
    char* res = NULL;
    while (fgets(line, sizeof(line), f)) {
        if (n == pc) { line[strcspn(line, "\n")] = '\0'; res = strdup(line); break; }
        n++;
    }
    fclose(f);
    return res;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main(int argc, char* argv[]) {
    if (argc < 2) { fprintf(stderr, "Uso: %s [Config]\n", argv[0]); return EXIT_FAILURE; }

    config = config_create(argv[1]);
    if (!config) { fprintf(stderr, "Error leyendo config\n"); return EXIT_FAILURE; }

    logger = log_create("kernel_memory.log", "KernelMemory", true, log_level_from_string(config_get_string_value(config, "LOG_LEVEL")));
    if (!logger) { fprintf(stderr, "Error creando logger\n"); return EXIT_FAILURE; }

    contextos     = list_create();
    memory_sticks = list_create();
    huecos        = list_create();
    swap_metadata = list_create();
    script_paths  = list_create();

    log_debug(logger, "Config resuelta: SCRIPTS_BASEPATH=%s ALLOCATION_STRATEGY=%s SEGMENT_MAX_SIZE=%d",
              config_get_string_value(config, "SCRIPTS_BASEPATH"),
              config_get_string_value(config, "ALLOCATION_STRATEGY"),
              config_get_int_value(config, "SEGMENT_MAX_SIZE"));

    int puerto = config_get_int_value(config, "KERNEL_MEMORY_PORT");
    int srv    = crear_servidor(puerto);
    if (srv < 0) {
        log_error(logger, "No se pudo levantar servidor en puerto %d", puerto);
        return EXIT_FAILURE;
    }

    log_info(logger, "Kernel Memory escuchando en puerto %d", puerto);

    while (1) {
        int cli = aceptar_conexion(srv);
        if (cli < 0) { log_warning(logger, "Error aceptando cliente"); continue; }
        t_args* a = malloc(sizeof(t_args));
        a->fd = cli;
        pthread_t hilo;
        pthread_create(&hilo, NULL, atender_cliente, a);
        pthread_detach(hilo);
    }

    return EXIT_SUCCESS;
}
