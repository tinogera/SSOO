#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <commons/log.h>
#include <commons/config.h>
#include <commons/collections/list.h>
#include <utils/sockets.h>
#include <utils/protocolo.h>

// ---------------------------------------------------------------------------
// Globales
// ---------------------------------------------------------------------------
t_log*    logger;
t_config* config;

pthread_mutex_t mutex_contextos = PTHREAD_MUTEX_INITIALIZER;
t_list*         contextos; // lista de t_contexto*

// ---------------------------------------------------------------------------
// Buscar contexto por PID — requiere mutex tomado
// ---------------------------------------------------------------------------
static t_contexto* buscar_contexto(uint32_t pid) {
    for (int i = 0; i < list_size(contextos); i++) {
        t_contexto* ctx = list_get(contextos, i);
        if (ctx->pid == pid) return ctx;
    }
    return NULL;
}

// ---------------------------------------------------------------------------
// Leer instrucción en la línea PC del archivo del PID
// Retorna string en heap, NULL si falla
// ---------------------------------------------------------------------------
static char* leer_instruccion(uint32_t pid, uint32_t pc) {
    char* base = config_get_string_value(config, "SCRIPTS_BASEPATH");

    char path[512];
    snprintf(path, sizeof(path), "%s/%u.txt", base, pid);

    FILE* f = fopen(path, "r");
    if (f == NULL) {
        log_error(logger, "No se pudo abrir archivo de instrucciones: %s", path);
        return NULL;
    }

    char line[256];
    uint32_t linea_actual = 0;
    char* resultado = NULL;

    while (fgets(line, sizeof(line), f)) {
        if (linea_actual == pc) {
            line[strcspn(line, "\n")] = '\0';
            resultado = strdup(line);
            break;
        }
        linea_actual++;
    }
    fclose(f);
    return resultado;
}

// ---------------------------------------------------------------------------
// Hilo por cliente
// ---------------------------------------------------------------------------
typedef struct { int fd; } t_args;

static void* atender_cliente(void* arg) {
    t_args* args = (t_args*)arg;
    int fd = args->fd;
    free(args);

    // ------------------------------------------------------------------
    // Identificación inicial
    // ------------------------------------------------------------------
    t_mensaje* msg = recibir_mensaje(fd);
    if (msg == NULL) {
        log_warning(logger, "Cliente desconectado antes de identificarse (fd=%d)", fd);
        return NULL;
    }

    int identificado = 1;

    switch (msg->op_code) {

        case MSG_KS_IDENTIFICACION:
            log_info(logger, "## Kernel Scheduler Conectado - FD del socket: %d", fd);
            enviar_mensaje(fd, MSG_OK, NULL, 0);
            break;

        case MSG_MEMORY_STICK_IDENTIFICACION: {
            if (msg->payload_size >= 4) {
                uint32_t tamanio_n;
                memcpy(&tamanio_n, msg->payload, 4);
                uint32_t tamanio = ntohl(tamanio_n);
                log_info(logger, "## Memory Stick de %u bytes Conectada", tamanio);
            } else {
                log_warning(logger, "Memory Stick conectado sin payload (fd=%d)", fd);
            }
            enviar_mensaje(fd, MSG_OK, NULL, 0);
            break;
        }

        case MSG_SWAP_IDENTIFICACION: {
            if (msg->payload_size >= 8) {
                uint32_t swap_size_n, block_size_n;
                memcpy(&swap_size_n,  msg->payload,     4);
                memcpy(&block_size_n, (uint8_t*)msg->payload + 4, 4);
                uint32_t swap_size    = ntohl(swap_size_n);
                uint32_t block_size   = ntohl(block_size_n);
                uint32_t cant_bloques = swap_size / block_size;
                log_info(logger,
                    "## Swap Conectado - FD: %d - Tamaño: %u bytes - Bloque: %u bytes - Bloques totales: %u",
                    fd, swap_size, block_size, cant_bloques);
            } else {
                log_warning(logger, "Swap conectado sin payload (fd=%d)", fd);
            }
            enviar_mensaje(fd, MSG_OK, NULL, 0);
            break;
        }

        case MSG_CPU_IDENTIFICACION: {
            if (msg->payload_size >= 4) {
                uint32_t cpu_id_n;
                memcpy(&cpu_id_n, msg->payload, 4);
                uint32_t cpu_id = ntohl(cpu_id_n);
                log_info(logger, "## CPU %u Conectada", cpu_id);
            } else {
                log_info(logger, "## CPU Conectada - FD del socket: %d", fd);
            }
            enviar_mensaje(fd, MSG_OK, NULL, 0);
            break;
        }

        default:
            log_warning(logger, "op_code de identificación desconocido: %d (fd=%d)", msg->op_code, fd);
            enviar_mensaje(fd, MSG_ERROR, NULL, 0);
            identificado = 0;
            break;
    }

    free_mensaje(msg);
    if (!identificado) return NULL;

    // ------------------------------------------------------------------
    // Loop de atención de pedidos
    // ------------------------------------------------------------------
    t_mensaje* pedido;
    while ((pedido = recibir_mensaje(fd)) != NULL) {

        // --------------------------------------------------------------
        // CREAR PROCESO
        // --------------------------------------------------------------
        if (pedido->op_code == MSG_CREAR_PROCESO) {
            // Payload: [pid: 4 bytes big-endian] [path: string con \0]
            if (pedido->payload_size < 5) {
                log_warning(logger, "MSG_CREAR_PROCESO con payload inválido");
                enviar_mensaje(fd, MSG_ERROR, NULL, 0);
                free_mensaje(pedido);
                continue;
            }

            uint32_t pid_n;
            memcpy(&pid_n, pedido->payload, 4);
            uint32_t pid = ntohl(pid_n);

            pthread_mutex_lock(&mutex_contextos);
            if (buscar_contexto(pid) != NULL) {
                log_warning(logger, "PID %u ya existe", pid);
                pthread_mutex_unlock(&mutex_contextos);
                enviar_mensaje(fd, MSG_ERROR, NULL, 0);
                free_mensaje(pedido);
                continue;
            }

            t_contexto* ctx = malloc(sizeof(t_contexto));
            memset(ctx, 0, sizeof(t_contexto));
            ctx->pid            = pid;
            ctx->cant_segmentos = 0;
            ctx->segmentos      = NULL;
            list_add(contextos, ctx);
            pthread_mutex_unlock(&mutex_contextos);

            log_info(logger, "## PID: %u - Proceso Creado", pid);
            enviar_mensaje(fd, MSG_OK, NULL, 0);
        }

        // --------------------------------------------------------------
        // FETCH INSTRUCCIÓN
        // --------------------------------------------------------------
        else if (pedido->op_code == MSG_FETCH_INSTRUCCION) {
            if (pedido->payload_size < 8) {
                log_warning(logger, "MSG_FETCH_INSTRUCCION con payload inválido");
                enviar_mensaje(fd, MSG_ERROR, NULL, 0);
                free_mensaje(pedido);
                continue;
            }

            t_fetch_request* req = deserializar_fetch_request(pedido->payload);

            int delay_ms = config_get_int_value(config, "INSTRUCTION_DELAY");
            usleep(delay_ms * 1000);

            char* instruccion = leer_instruccion(req->pid, req->pc);
            if (instruccion == NULL) {
                enviar_mensaje(fd, MSG_ERROR, NULL, 0);
            } else {
                log_info(logger, "## PID: %u - Obtener instrucción: %u - Instrucción: %s",
                         req->pid, req->pc, instruccion);

                uint32_t size;
                void* payload = serializar_string(instruccion, &size);
                enviar_mensaje(fd, MSG_RESPUESTA_INSTRUCCION, payload, size);
                free(payload);
                free(instruccion);
            }
            free(req);
        }

        // --------------------------------------------------------------
        // GUARDAR CONTEXTO
        // --------------------------------------------------------------
        else if (pedido->op_code == MSG_GUARDAR_CONTEXTO) {
            t_contexto* nuevo = deserializar_contexto(pedido->payload, pedido->payload_size);

            pthread_mutex_lock(&mutex_contextos);
            t_contexto* existente = buscar_contexto(nuevo->pid);
            if (existente == NULL) {
                log_warning(logger, "Guardar contexto: PID %u no existe", nuevo->pid);
                pthread_mutex_unlock(&mutex_contextos);
                enviar_mensaje(fd, MSG_ERROR, NULL, 0);
                free_contexto(nuevo);
                free_mensaje(pedido);
                continue;
            }

            existente->registros = nuevo->registros;
            if (existente->segmentos) free(existente->segmentos);
            existente->cant_segmentos = nuevo->cant_segmentos;
            existente->segmentos      = nuevo->segmentos;
            nuevo->segmentos = NULL; // evitar doble free
            pthread_mutex_unlock(&mutex_contextos);

            enviar_mensaje(fd, MSG_OK, NULL, 0);
            free_contexto(nuevo);
        }

        // --------------------------------------------------------------
        // RESTAURAR CONTEXTO
        // --------------------------------------------------------------
        else if (pedido->op_code == MSG_RESTAURAR_CONTEXTO) {
            if (pedido->payload_size < 4) {
                log_warning(logger, "MSG_RESTAURAR_CONTEXTO con payload inválido");
                enviar_mensaje(fd, MSG_ERROR, NULL, 0);
                free_mensaje(pedido);
                continue;
            }

            uint32_t pid_n;
            memcpy(&pid_n, pedido->payload, 4);
            uint32_t pid = ntohl(pid_n);

            pthread_mutex_lock(&mutex_contextos);
            t_contexto* ctx = buscar_contexto(pid);
            if (ctx == NULL) {
                pthread_mutex_unlock(&mutex_contextos);
                log_warning(logger, "Restaurar contexto: PID %u no existe", pid);
                enviar_mensaje(fd, MSG_ERROR, NULL, 0);
                free_mensaje(pedido);
                continue;
            }

            uint32_t size;
            void* payload = serializar_contexto(ctx, &size);
            pthread_mutex_unlock(&mutex_contextos);

            enviar_mensaje(fd, MSG_RESTAURAR_CONTEXTO, payload, size);
            free(payload);
        }

        else {
            log_warning(logger, "op_code desconocido en loop: %d", pedido->op_code);
            enviar_mensaje(fd, MSG_ERROR, NULL, 0);
        }

        free_mensaje(pedido);
    }

    log_info(logger, "Cliente desconectado (fd=%d)", fd);
    return NULL;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Uso: %s [Archivo Config]\n", argv[0]);
        return EXIT_FAILURE;
    }

    config = config_create(argv[1]);
    if (config == NULL) {
        fprintf(stderr, "No se pudo leer el config: %s\n", argv[1]);
        return EXIT_FAILURE;
    }

    logger = log_create("kernel_memory.log", "KernelMemory", true, LOG_LEVEL_INFO);
    if (logger == NULL) {
        fprintf(stderr, "Error al crear el logger\n");
        config_destroy(config);
        return EXIT_FAILURE;
    }

    contextos = list_create();

    int puerto = config_get_int_value(config, "KERNEL_MEMORY_PORT");

    int servidor_fd = crear_servidor(puerto);
    if (servidor_fd < 0) {
        log_error(logger, "No se pudo levantar el servidor en puerto %d", puerto);
        config_destroy(config);
        log_destroy(logger);
        return EXIT_FAILURE;
    }

    log_info(logger, "Kernel Memory escuchando en puerto %d", puerto);

    while (1) {
        int cliente_fd = aceptar_conexion(servidor_fd);
        if (cliente_fd < 0) {
            log_warning(logger, "Error al aceptar cliente");
            continue;
        }

        t_args* args = malloc(sizeof(t_args));
        args->fd = cliente_fd;

        pthread_t hilo;
        pthread_create(&hilo, NULL, atender_cliente, args);
        pthread_detach(hilo);
    }

    list_destroy_and_destroy_elements(contextos, (void*)free_contexto);
    config_destroy(config);
    log_destroy(logger);
    return EXIT_SUCCESS;
}
