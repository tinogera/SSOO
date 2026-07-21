#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <semaphore.h>
#include <time.h>
#include <commons/log.h>
#include <commons/collections/queue.h>
#include <commons/collections/list.h>
#include <commons/config.h>
#include <utils/sockets.h>
#include <utils/protocolo.h>
#include <proceso.h>
#include "ks_mutex.h"
#include "ks_cmn.h"
#include "ks_compact.h"
#include "ks_queue_search.h"

t_log* logger;
static int             fd_km          = -1;
static pthread_mutex_t mutex_km_req   = PTHREAD_MUTEX_INITIALIZER; // serializa envíos a KM
static pthread_mutex_t mutex_km_resp  = PTHREAD_MUTEX_INITIALIZER; // protege ultimo_resp_km
static pthread_cond_t  cond_km_resp   = PTHREAD_COND_INITIALIZER;
static t_mensaje*      ultimo_resp_km  = NULL;

// Compactación
static int             compactando       = 0;
static pthread_mutex_t mutex_compactando = PTHREAD_MUTEX_INITIALIZER;
static sem_t           sem_cpus_devueltas;  // señalado por cada CPU que devuelve durante compactación
static sem_t           sem_planificador_ok; // 1=normal, 0=compactando (bloquea thread_planificador)
// El MSG_FIN_COMPACTACION no puede ir por km_request: mutex_km_req está retenido
// por el manejar_mem_alloc que disparó la compactación. Se envía crudo (con
// mutex_km_send) y handle_compactar espera sem_fin_compactacion, que señala el
// hilo de MEM_ALLOC al recibir su respuesta (KM la envía recién tras compactar).
static sem_t           sem_fin_compactacion;
static int             esperando_fin_compactacion = 0; // protegido por mutex_compactando
static pthread_mutex_t mutex_km_send = PTHREAD_MUTEX_INITIALIZER; // serializa envíos crudos a fd_km

t_queue *cola_new, *cola_exec;
t_queue *cola_block, *cola_susp_block, *cola_susp_ready, *cola_exit;

pthread_mutex_t mutex_new, mutex_exec;
pthread_mutex_t mutex_block, mutex_susp_block, mutex_susp_ready, mutex_exit;

static int             n_colas = 1;
static char            algoritmos_cola[MAX_COLAS][8];
static t_queue*        colas_ready[MAX_COLAS];
static pthread_mutex_t mutex_colas_ready[MAX_COLAS];

static uint32_t        next_pid  = 0;
static pthread_mutex_t mutex_pid = PTHREAD_MUTEX_INITIALIZER;

static char algoritmo[8];
static int  rr_quantum_ms;
static int  suspension_timeout_ms;
static int  queue_preemption = 0;

typedef struct {
    int fd;
    int ocupada;
} t_cpu_entry;

// Args del timer de quantum: pid + generación de despacho. Un timer solo
// interrumpe si el proceso sigue en EXEC *del mismo despacho*; si el proceso
// se bloqueó y fue re-despachado rápido, el timer viejo no debe desalojarlo.
typedef struct {
    int pid;
    int gen;
} t_quantum_args;

// Args del timer de suspensión. La generación evita que el timer de una IO
// anterior suspenda por error un bloqueo posterior del mismo proceso.
typedef struct {
    int pid;
    int gen_bloqueo;
} t_suspension_args;

static t_list*         lista_cpus = NULL;
static pthread_mutex_t mutex_cpus = PTHREAD_MUTEX_INITIALIZER;
static sem_t           sem_cpu_disponible;
static sem_t           sem_largo_plazo;

static int             fd_io_sleep  = -1;
static int             fd_io_stdout = -1;
static int             fd_io_stdin  = -1;
static pthread_mutex_t mutex_io     = PTHREAD_MUTEX_INITIALIZER;

typedef struct {
    int      pid;
    uint32_t dir_logica;
    uint32_t tamanio;
    uint8_t* datos;
    uint32_t datos_size;
    bool     datos_recibidos;
} t_stdin_pendiente;

static t_list*         lista_stdin_pendientes = NULL;
static pthread_mutex_t mutex_stdin_pendientes = PTHREAD_MUTEX_INITIALIZER;
// Serializa el fin de una IO con su timer de suspensión. Sin esto el proceso
// puede aparecer en SUSP_BLOCK antes de que KM confirme que lo suspendió.
static pthread_mutex_t mutex_transiciones_io = PTHREAD_MUTEX_INITIALIZER;

static t_mensaje* km_request(uint32_t op, void* payload, uint32_t size);
static void       avisar_km_finalizacion(int pid);
static void       manejar_bsod(void);
static void*      manejar_mas_memoria(void* _);
static void*      thread_km_listener(void* _);
t_proceso* crear_proceso(char* path, int prioridad);
void*      atender_cliente(void* arg);
static void atender_cpu(int fd, t_cpu_entry* entry);
static void atender_io(int fd, char* tipo);
static void* thread_planificador(void* _);
static void* thread_quantum_timer(void* arg);
static void* thread_largo_plazo(void* _);
static void* thread_suspension_timer(void* arg);
static void  handle_compactar(void);
static void* manejar_mem_alloc(void* arg);
static void* manejar_mem_free(void* arg);

// Encola el proceso en la cola de su prioridad y señala al planificador.
static void encolar_en_ready(t_proceso* proc);

// Encola al FRENTE de la cola (para procesos desalojados por preemption o compactación).
static void encolar_al_frente_en_ready(t_proceso* proc) {
    int nivel = proc->prioridad;
    if (nivel < 0 || nivel >= n_colas) nivel = n_colas - 1;
    pthread_mutex_lock(&mutex_colas_ready[nivel]);
    list_add_in_index(colas_ready[nivel]->elements, 0, proc);
    pthread_mutex_unlock(&mutex_colas_ready[nivel]);
    sem_post(&sem_cpu_disponible);
}

// Si QUEUE_PREEMPTION está activo, interrumpe el proceso en EXEC con menor prioridad
// que 'entrante' (si existe).
static void verificar_preemption(t_proceso* entrante) {
    if (!queue_preemption) return;

    pthread_mutex_lock(&mutex_exec);
    t_proceso* victima = NULL;
    for (int i = 0; i < (int)queue_size(cola_exec); i++) {
        t_proceso* p = list_get(cola_exec->elements, i);
        if (p->prioridad > entrante->prioridad) {
            victima = p;
            break;
        }
    }
    if (victima) {
        victima->preemptado = 1;
        log_info(logger,
            "## (%d) Prioridad: %d - Desalojado por cola más prioritaria por el proceso %d con prioridad %d",
            victima->PID, victima->prioridad, entrante->PID, entrante->prioridad);
        t_payload_interrupcion_cpu pay = {
            .pid    = htonl(victima->PID),
            .motivo = htonl(MOTIVO_INTERRUPCION_DESALOJO)
        };
        enviar_mensaje(victima->fd_cpu, MSG_INTERRUPCION_CPU, &pay, sizeof(pay));
    }
    pthread_mutex_unlock(&mutex_exec);
}

static void encolar_en_ready(t_proceso* proc) {
    int nivel = proc->prioridad;
    if (nivel < 0 || nivel >= n_colas) nivel = n_colas - 1;
    pthread_mutex_lock(&mutex_colas_ready[nivel]);
    queue_push(colas_ready[nivel], proc);
    pthread_mutex_unlock(&mutex_colas_ready[nivel]);
    sem_post(&sem_cpu_disponible);
    verificar_preemption(proc);
}

static const char* estado_str(t_estado e) {
    switch (e) {
        case NEW:        return "NEW";
        case READY:      return "READY";
<        case EXEC:       return "EXEC";
        case BLOCK:      return "BLOCK";
        case SUSP_BLOCK: return "SUSP. BLOCK";
        case SUSP_READY: return "SUSP. READY";
        case EXIT:       return "EXIT";
        default:         return "UNKNOWN";
    }
}

static void cambiar_estado(t_proceso* proc, t_estado nuevo) {
    log_info(logger, "## (%d) Pasa del estado %s al estado %s",
             proc->PID, estado_str(proc->estado), estado_str(nuevo));
    proc->estado = nuevo;
}

static void destruir_stdin_pendiente(void* elem) {
    t_stdin_pendiente* sp = elem;
    if (!sp) return;
    free(sp->datos);
    free(sp);
}

// Extrae el pedido de la lista y transfiere ownership al llamador.
static t_stdin_pendiente* extraer_stdin_pendiente(int pid) {
    t_stdin_pendiente* encontrado = NULL;
    pthread_mutex_lock(&mutex_stdin_pendientes);
    for (int i = 0; i < list_size(lista_stdin_pendientes); i++) {
        t_stdin_pendiente* sp = list_get(lista_stdin_pendientes, i);
        if (sp->pid == pid) {
            encontrado = list_remove(lista_stdin_pendientes, i);
            break;
        }
    }
    pthread_mutex_unlock(&mutex_stdin_pendientes);
    return encontrado;
}

// Escribe el resultado de STDIN solamente cuando el proceso está residente.
// Retorna 0 si KM confirmó la escritura y -1 ante datos inválidos o error de KM.
static int escribir_stdin_en_km(const t_stdin_pendiente* sp) {
    if (!sp || !sp->datos_recibidos || sp->datos_size != sp->tamanio)
        return -1;
    if (sp->tamanio == 0)
        return 0;
    if (sp->tamanio > UINT32_MAX - 12u)
        return -1;

    uint32_t km_size = 12u + sp->tamanio;
    uint8_t* km_buf = malloc(km_size);
    if (!km_buf) return -1;

    uint32_t pid_n = htonl((uint32_t)sp->pid);
    uint32_t dl_n  = htonl(sp->dir_logica);
    uint32_t tam_n = htonl(sp->tamanio);
    memcpy(km_buf,      &pid_n, 4);
    memcpy(km_buf + 4,  &dl_n,  4);
    memcpy(km_buf + 8,  &tam_n, 4);
    memcpy(km_buf + 12, sp->datos, sp->tamanio);

    t_mensaje* resp = km_request(MSG_ESCRIBIR_DATOS, km_buf, km_size);
    int ok = resp && resp->op_code == MSG_OK;
    if (resp) free_mensaje(resp);
    free(km_buf);
    return ok ? 0 : -1;
}

// Un proceso cuyo resultado de IO no pudo persistirse no puede volver a READY:
// continuarlo haría que consumiera memoria con datos inexistentes o corruptos.
static void finalizar_por_error_io(t_proceso* proc, const char* detalle) {
    cambiar_estado(proc, EXIT);
    log_info(logger, "## (%d) finalizó su ejecución con motivo de ERROR", proc->PID);
    log_error(logger, "## (%d) - IO: %s", proc->PID, detalle);
    pthread_mutex_lock(&mutex_exit);
    queue_push(cola_exit, proc);
    pthread_mutex_unlock(&mutex_exit);
    avisar_km_finalizacion(proc->PID);
}


static void finalizar_proceso_bsod(t_proceso* proc) {
    cambiar_estado(proc, EXIT);
    log_info(logger, "## (%d) finalizó su ejecución con motivo de BSOD", proc->PID);
    pthread_mutex_lock(&mutex_exit);
    queue_push(cola_exit, proc);
    pthread_mutex_unlock(&mutex_exit);
    avisar_km_finalizacion(proc->PID);
}

static void manejar_bsod(void) {
    log_warning(logger, "## BSOD: Memory Stick desconectado — finalizando todos los procesos");

    // Interrumpir CPUs en EXEC antes de vaciar la cola
    pthread_mutex_lock(&mutex_exec);
    int sz = queue_size(cola_exec);
    for (int i = 0; i < sz; i++) {
        t_proceso* proc = queue_pop(cola_exec);
        t_payload_interrupcion_cpu pay = {
            .pid    = htonl((uint32_t)proc->PID),
            .motivo = htonl(MOTIVO_INTERRUPCION_DESALOJO)
        };
        enviar_mensaje(proc->fd_cpu, MSG_INTERRUPCION_CPU, &pay, sizeof(pay));
        proc->fd_cpu = -1;
        finalizar_proceso_bsod(proc);
    }
    pthread_mutex_unlock(&mutex_exec);

    // Vaciar el resto de las colas
    for (int i = 0; i < n_colas; i++) {
        pthread_mutex_lock(&mutex_colas_ready[i]);
        while (queue_size(colas_ready[i]) > 0)
            finalizar_proceso_bsod(queue_pop(colas_ready[i]));
        pthread_mutex_unlock(&mutex_colas_ready[i]);
    }

    pthread_mutex_lock(&mutex_block);
    while (queue_size(cola_block) > 0)
        finalizar_proceso_bsod(queue_pop(cola_block));
    pthread_mutex_unlock(&mutex_block);

    pthread_mutex_lock(&mutex_susp_block);
    while (queue_size(cola_susp_block) > 0)
        finalizar_proceso_bsod(queue_pop(cola_susp_block));
    pthread_mutex_unlock(&mutex_susp_block);

    pthread_mutex_lock(&mutex_susp_ready);
    while (queue_size(cola_susp_ready) > 0)
        finalizar_proceso_bsod(queue_pop(cola_susp_ready));
    pthread_mutex_unlock(&mutex_susp_ready);

    log_warning(logger, "## BSOD: sistema finalizado");
    exit(EXIT_FAILURE);
}

static bool cmp_suspension(void* a, void* b) {
    t_proceso* pa = (t_proceso*)a;
    t_proceso* pb = (t_proceso*)b;
    if (pa->prioridad != pb->prioridad)
        return pa->prioridad < pb->prioridad;          // mayor prioridad (número menor) primero
    return pa->tiempo_suspension < pb->tiempo_suspension; // más viejo primero
}

static void* manejar_mas_memoria(void* _) {
    (void)_;
    // Vaciar cola_susp_ready en una lista local para ordenar y procesar
    pthread_mutex_lock(&mutex_susp_ready);
    t_list* candidatos = list_create();
    while (queue_size(cola_susp_ready) > 0)
        list_add(candidatos, queue_pop(cola_susp_ready));
    pthread_mutex_unlock(&mutex_susp_ready);

    if (list_size(candidatos) == 0) {
        list_destroy(candidatos);
        return NULL;
    }

    list_sort(candidatos, cmp_suspension);

    for (int i = 0; i < list_size(candidatos); i++) {
        t_proceso* proc = list_get(candidatos, i);

        uint32_t pid_n = htonl((uint32_t)proc->PID);
        t_mensaje* resp = km_request(MSG_DESSUSPENDER_PROCESO, &pid_n, sizeof(uint32_t));

        if (resp && resp->op_code == MSG_OK) {
            free_mensaje(resp);

            // Si llegó un STDIN mientras el proceso estaba suspendido, sus
            // bytes quedaron retenidos en KS. Ahora que KM restauró todos los
            // segmentos se pueden escribir de forma segura.
            if (proc->esperando_stdin) {
                t_stdin_pendiente* sp = extraer_stdin_pendiente(proc->PID);
                int escritura_ok = escribir_stdin_en_km(sp) == 0;
                destruir_stdin_pendiente(sp);
                proc->esperando_stdin = 0;
                if (!escritura_ok) {
                    finalizar_por_error_io(proc,
                        "no se pudo persistir el resultado de STDIN después de desuspender");
                    continue;
                }
            }

            cambiar_estado(proc, READY);
            encolar_en_ready(proc);
        } else {
            // Sin espacio: este proceso sigue suspendido, pero se prueban los
            // restantes porque alguno más pequeño sí podría caber.
            if (resp) free_mensaje(resp);
            pthread_mutex_lock(&mutex_susp_ready);
            queue_push(cola_susp_ready, proc);
            pthread_mutex_unlock(&mutex_susp_ready);
        }
    }

    list_destroy(candidatos);
    return NULL;
}

static t_mensaje* km_request(uint32_t op, void* payload, uint32_t size) {
    pthread_mutex_lock(&mutex_km_req);
    pthread_mutex_lock(&mutex_km_send);
    enviar_mensaje(fd_km, op, payload, size);
    pthread_mutex_unlock(&mutex_km_send);

    pthread_mutex_lock(&mutex_km_resp);
    while (ultimo_resp_km == NULL)
        pthread_cond_wait(&cond_km_resp, &mutex_km_resp);
    t_mensaje* resp = ultimo_resp_km;
    ultimo_resp_km = NULL;
    pthread_mutex_unlock(&mutex_km_resp);

    pthread_mutex_unlock(&mutex_km_req);
    return resp;
}

// Avisa a KM que el proceso terminó (EXIT/ERROR/SEG_FAULT/BSOD) para que
// libere los segmentos + contexto que tenía asignados. Sin esto esa memoria
// queda perdida para siempre: KM no tiene forma propia de enterarse.
static void avisar_km_finalizacion(int pid) {
    uint32_t pid_n = htonl((uint32_t)pid);
    t_mensaje* resp = km_request(MSG_FINALIZAR_PROCESO, &pid_n, sizeof(uint32_t));
    if (!resp || resp->op_code != MSG_OK) {
        log_error(logger, "KM no confirmó la liberación de memoria del proceso %d", pid);
    } else {
        sem_post(&sem_largo_plazo);
    }
    if (resp) free_mensaje(resp);
}

static void* thread_km_listener(void* _) {
    (void)_;
    while (1) {
        t_mensaje* msg = recibir_mensaje(fd_km);
        if (!msg) {
            log_error(logger, "KM cerró la conexión");
            break;
        }
        switch (msg->op_code) {
        case MSG_OK:
        case MSG_ERROR:
        case MSG_TABLA_SEGMENTOS:
        case MSG_LEER_DATOS_RESP:
            pthread_mutex_lock(&mutex_km_resp);
            ultimo_resp_km = msg;
            pthread_cond_signal(&cond_km_resp);
            pthread_mutex_unlock(&mutex_km_resp);
            break;
        case MSG_COMPACTAR:
            free_mensaje(msg);
            {
                // Despachamos en un hilo separado para que el listener quede libre
                // de recibir el MSG_OK que KM enviará al final de la compactación.
                // Llamar handle_compactar() directo causaría deadlock: el listener
                // quedaría bloqueado esperando una respuesta que él mismo debería leer.
                pthread_t t;
                pthread_create(&t, NULL, (void*(*)(void*))handle_compactar, NULL);
                pthread_detach(t);
            }
            break;
        case MSG_MAS_MEMORIA:
            free_mensaje(msg);
            // Un único worker procesa SUSP_READY. Evita dos des-suspensiones
            // concurrentes y conserva el orden por prioridad/antigüedad.
            sem_post(&sem_largo_plazo);
            break;
        case MSG_BSOD:
            free_mensaje(msg);
            manejar_bsod();
            break;
        default:
            log_warning(logger, "KM: op_code inesperado %u", msg->op_code);
            free_mensaje(msg);
        }
    }
    return NULL;
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Uso: %s [Archivo Config] [Path Proceso Inicial]\n", argv[0]);
        return EXIT_FAILURE;
    }

    t_config* config = config_create(argv[1]);
    if (!config) { fprintf(stderr, "No se pudo leer config: %s\n", argv[1]); return EXIT_FAILURE; }

    char* log_level_str = config_get_string_value(config, "LOG_LEVEL");
    logger = log_create("kernel_scheduler.log", "KernelScheduler", true, log_level_from_string(log_level_str));
    if (!logger) { fprintf(stderr, "Error al crear logger\n"); config_destroy(config); return EXIT_FAILURE; }

    char* ip_km   = config_get_string_value(config, "KERNEL_MEMORY_IP");
    int puerto_km = config_get_int_value(config, "KERNEL_MEMORY_PORT");
    int puerto_ks = config_get_int_value(config, "KERNEL_SCHEDULER_PORT");
    strncpy(algoritmo, config_get_string_value(config, "PLANIFICATION_ALGORITHM"), sizeof(algoritmo) - 1);
    rr_quantum_ms         = config_get_int_value(config, "RR_QUANTUM");
    suspension_timeout_ms = config_get_int_value(config, "SUSPENSION_TIMEOUT");

    if (strcmp(algoritmo, "CMN") == 0) {
        char* qs = config_get_string_value(config, "QUEUES_ALGORITHMS");
        n_colas = parsear_queues_algorithms(qs, algoritmos_cola);
        char* qp = config_get_string_value(config, "QUEUE_PREEMPTION");
        queue_preemption = (strcmp(qp, "TRUE") == 0) ? 1 : 0;
    } else {
        n_colas = 1;
        strncpy(algoritmos_cola[0], algoritmo, sizeof(algoritmos_cola[0]) - 1);
    }

    // Log de diagnóstico: la config de planificación ya resuelta, tal como
    // va a operar el KS (útil para confirmar que un test corre con el
    // algoritmo/preemption esperado sin tener que abrir el .config a mano).
    {
        char colas_str[MAX_COLAS * 8] = "";
        for (int i = 0; i < n_colas; i++) {
            strcat(colas_str, algoritmos_cola[i]);
            if (i < n_colas - 1) strcat(colas_str, ",");
        }
        log_debug(logger,
            "Planificación resuelta: algoritmo=%s n_colas=%d colas=[%s] RR_QUANTUM=%dms QUEUE_PREEMPTION=%s SUSPENSION_TIMEOUT=%dms",
            algoritmo, n_colas, colas_str, rr_quantum_ms,
            queue_preemption ? "TRUE" : "FALSE", suspension_timeout_ms);
    }

    // Conectar a KM sincrónicamente antes de crear PID 0
    fd_km = conectar_a_servidor(ip_km, puerto_km);
    if (fd_km == -1) {
        log_error(logger, "No se pudo conectar a Kernel Memory en %s:%d", ip_km, puerto_km);
        config_destroy(config); log_destroy(logger); return EXIT_FAILURE;
    }
    enviar_mensaje(fd_km, MSG_KS_IDENTIFICACION, NULL, 0);
    t_mensaje* resp_km = recibir_mensaje(fd_km);
    if (!resp_km || resp_km->op_code != MSG_OK) {
        log_error(logger, "Kernel Memory rechazo la conexion");
        if (resp_km) free_mensaje(resp_km);
        config_destroy(config); log_destroy(logger); return EXIT_FAILURE;
    }
    free_mensaje(resp_km);
    log_info(logger, "## Conectado a Kernel Memory");

    // Inicializar colas
    cola_new        = queue_create();
    cola_exec       = queue_create();
    cola_block      = queue_create();
    cola_susp_block = queue_create();
    cola_susp_ready = queue_create();
    cola_exit       = queue_create();

    pthread_mutex_init(&mutex_new,        NULL);
    pthread_mutex_init(&mutex_exec,       NULL);
    pthread_mutex_init(&mutex_block,      NULL);
    pthread_mutex_init(&mutex_susp_block, NULL);
    pthread_mutex_init(&mutex_susp_ready, NULL);
    pthread_mutex_init(&mutex_exit,       NULL);

    for (int i = 0; i < n_colas; i++) {
        colas_ready[i] = queue_create();
        pthread_mutex_init(&mutex_colas_ready[i], NULL);
    }

    lista_cpus             = list_create();
    lista_stdin_pendientes = list_create();
    sem_init(&sem_cpu_disponible,  0, 0);
    sem_init(&sem_largo_plazo,     0, 0);
    sem_init(&sem_cpus_devueltas,  0, 0);
    sem_init(&sem_planificador_ok, 0, 1);
    sem_init(&sem_fin_compactacion, 0, 0);
    mutexes_init();

    // Servidor
    int fd_servidor = crear_servidor(puerto_ks);
    if (fd_servidor < 0) {
        log_error(logger, "Error al crear servidor en puerto %d", puerto_ks);
        config_destroy(config); log_destroy(logger); return EXIT_FAILURE;
    }

    // Lanzar listener de KM (toma ownership de fd_km desde aquí)
    pthread_t t_km;
    pthread_create(&t_km, NULL, thread_km_listener, NULL);
    pthread_detach(t_km);

    // Lanzar thread planificador
    pthread_t t_plan;
    pthread_create(&t_plan, NULL, thread_planificador, NULL);
    pthread_detach(t_plan);

    pthread_t t_largo;
    pthread_create(&t_largo, NULL, thread_largo_plazo, NULL);
    pthread_detach(t_largo);

    // PID 0
    if (!crear_proceso(argv[2], 0)) {
        log_error(logger, "No se pudo crear el proceso inicial");
        config_destroy(config); log_destroy(logger); return EXIT_FAILURE;
    }

    // Accept loop
    while (1) {
        int fd_cliente = aceptar_conexion(fd_servidor);
        int* fd_mem = malloc(sizeof(int));
        *fd_mem = fd_cliente;
        pthread_t hilo;
        pthread_create(&hilo, NULL, atender_cliente, fd_mem);
        pthread_detach(hilo);
    }

    config_destroy(config);
    log_destroy(logger);
    return EXIT_SUCCESS;
}

static void handle_compactar(void) {
    log_info(logger, "## Inicio de compactación");

    // Bloquear el planificador para que no despache durante la compactación.
    sem_wait(&sem_planificador_ok);

    pthread_mutex_lock(&mutex_compactando);
    compactando = 1;
    pthread_mutex_unlock(&mutex_compactando);

    // Interrumpir todas las CPUs que estén ejecutando un proceso.
    pthread_mutex_lock(&mutex_exec);
    int n = (int)queue_size(cola_exec);
    for (int i = 0; i < n; i++) {
        t_proceso* p = list_get(cola_exec->elements, i);
        p->preemptado = 1;
        t_payload_interrupcion_cpu pay = {
            .pid    = htonl(p->PID),
            .motivo = htonl(MOTIVO_INTERRUPCION_DESALOJO)
        };
        enviar_mensaje(p->fd_cpu, MSG_INTERRUPCION_CPU, &pay, sizeof(pay));
    }
    pthread_mutex_unlock(&mutex_exec);

    // Esperar a que todas las CPUs devuelvan sus procesos.
    for (int i = 0; i < n; i++) {
        sem_wait(&sem_cpus_devueltas);
    }

    // Notificar a KM que las CPUs están ociosas. No se puede usar km_request:
    // mutex_km_req lo retiene el manejar_mem_alloc que disparó la compactación
    // (bloqueado esperando su MSG_TABLA_SEGMENTOS) — pedirlo acá deadlockearía.
    // KM responde el CREAR_SEGMENTO pendiente recién después de compactar, así
    // que el hilo de MEM_ALLOC nos señala sem_fin_compactacion al recibirla.
    pthread_mutex_lock(&mutex_compactando);
    esperando_fin_compactacion = 1;
    pthread_mutex_unlock(&mutex_compactando);

    pthread_mutex_lock(&mutex_km_send);
    enviar_mensaje(fd_km, MSG_FIN_COMPACTACION, NULL, 0);
    pthread_mutex_unlock(&mutex_km_send);

    sem_wait(&sem_fin_compactacion);

    pthread_mutex_lock(&mutex_compactando);
    compactando = 0;
    pthread_mutex_unlock(&mutex_compactando);

    log_info(logger, "## Fin de compactación");

    // Habilitar el planificador y señalarlo para que redespaché los procesos en READY.
    sem_post(&sem_planificador_ok);
    sem_post(&sem_cpu_disponible);
    sem_post(&sem_largo_plazo);
}

t_proceso* crear_proceso(char* path, int prioridad) {
    pthread_mutex_lock(&mutex_pid);
    uint32_t pid = next_pid++;
    pthread_mutex_unlock(&mutex_pid);

    t_proceso* proc = malloc(sizeof(t_proceso));
    proc->PID                    = (int)pid;
    proc->estado                 = NEW;
    proc->controladorDeProgramas = 0;
    proc->prioridad              = prioridad;
    proc->fd_cpu                 = -1;
    proc->preemptado             = 0;
    proc->gen_despacho           = 0;
    proc->gen_bloqueo            = 0;
    proc->esperando_stdin        = 0;
    proc->tiempo_suspension      = 0;

    log_info(logger, "## (%u) Se crea el proceso - Estado: NEW", pid);

    uint32_t path_len     = strlen(path) + 1;
    uint32_t payload_size = sizeof(uint32_t) + path_len;
    void*    payload      = malloc(payload_size);
    uint32_t pid_n        = htonl(pid);
    memcpy(payload, &pid_n, sizeof(uint32_t));
    memcpy((char*)payload + sizeof(uint32_t), path, path_len);

    t_mensaje* resp = km_request(MSG_CREAR_PROCESO, payload, payload_size);
    free(payload);

    if (!resp || resp->op_code != MSG_OK) {
        log_error(logger, "KM rechazo la creacion del proceso %u", pid);
        if (resp) free_mensaje(resp);
        free(proc); return NULL;
    }
    free_mensaje(resp);

    cambiar_estado(proc, READY);
    encolar_en_ready(proc);
    return proc;
}


void* atender_cliente(void* arg) {
    int fd = *((int*)arg);
    free(arg);

    t_mensaje* msg = recibir_mensaje(fd);
    if (!msg) return NULL;

    if (msg->op_code == MSG_CPU_IDENTIFICACION) {
        char* id_cpu = deserializar_string(msg->payload);
        log_info(logger, "## CPU %s Conectada", id_cpu);
        free(id_cpu);
        free_mensaje(msg);
        enviar_mensaje(fd, MSG_OK, NULL, 0);

        t_cpu_entry* entry = malloc(sizeof(t_cpu_entry));
        entry->fd      = fd;
        entry->ocupada = 0;
        pthread_mutex_lock(&mutex_cpus);
        list_add(lista_cpus, entry);
        pthread_mutex_unlock(&mutex_cpus);

        sem_post(&sem_cpu_disponible);
        atender_cpu(fd, entry);

    } else if (msg->op_code == MSG_IO_IDENTIFICACION) {
        char* tipo = deserializar_string(msg->payload);
        log_info(logger, "## IO %s Conectada", tipo);
        free_mensaje(msg);
        enviar_mensaje(fd, MSG_OK, NULL, 0);

        pthread_mutex_lock(&mutex_io);
        if      (strcmp(tipo, "SLEEP")  == 0) fd_io_sleep  = fd;
        else if (strcmp(tipo, "STDOUT") == 0) fd_io_stdout = fd;
        else if (strcmp(tipo, "STDIN")  == 0) fd_io_stdin  = fd;
        pthread_mutex_unlock(&mutex_io);

        atender_io(fd, tipo);
        free(tipo);
    } else {
        log_warning(logger, "Identificacion desconocida: op_code=%u", msg->op_code);
        free_mensaje(msg);
    }

    return NULL;
}


static void despachar(t_proceso* proc, t_cpu_entry* cpu) {
    cambiar_estado(proc, EXEC);
    proc->fd_cpu = cpu->fd;
    proc->gen_despacho++;
    cpu->ocupada  = 1;

    pthread_mutex_lock(&mutex_exec);
    queue_push(cola_exec, proc);
    pthread_mutex_unlock(&mutex_exec);

    t_payload_despachar_proceso payload = { .pid = htonl(proc->PID) };
    enviar_mensaje(cpu->fd, MSG_DESPACHAR_PROCESO, &payload, sizeof(payload));
}

static void* thread_quantum_timer(void* arg) {
    t_quantum_args qa = *((t_quantum_args*)arg);
    free(arg);

    struct timespec ts = {
        .tv_sec  = rr_quantum_ms / 1000,
        .tv_nsec = (long)(rr_quantum_ms % 1000) * 1000000L
    };
    nanosleep(&ts, NULL);

    // Si el proceso sigue en EXEC del MISMO despacho, interrumpir su CPU.
    // El envío tiene que hacerse con mutex_exec todavía tomado (mismo patrón
    // que verificar_preemption/manejar_bsod/handle_compactar): si se suelta
    // el lock antes de mandar el mensaje, el proceso puede salir de EXEC
    // (ej. bloquearse por un SLEEP) en el instante entre el check y el envío,
    // y la interrupción le llega a la CPU cuando ya está esperando el
    // despacho del PRÓXIMO proceso — ahí la CPU la loguea como mensaje
    // inesperado y la ignora (op_code=19 en recibir_proceso_a_ejecutar).
    pthread_mutex_lock(&mutex_exec);
    int fd_cpu_target = -1;
    for (int i = 0; i < (int)queue_size(cola_exec); i++) {
        t_proceso* p = list_get(cola_exec->elements, i);
        if (p->PID == qa.pid && p->gen_despacho == qa.gen) { fd_cpu_target = p->fd_cpu; break; }
    }
    if (fd_cpu_target != -1) {
        t_payload_interrupcion_cpu pay = { .pid = htonl(qa.pid), .motivo = htonl(MOTIVO_INTERRUPCION_QUANTUM) };
        enviar_mensaje(fd_cpu_target, MSG_INTERRUPCION_CPU, &pay, sizeof(pay));
    } else {
        log_debug(logger,
            "Timer de quantum descartado para (%d) gen=%d: ya no está en EXEC con esa generación de despacho",
            qa.pid, qa.gen);
    }
    pthread_mutex_unlock(&mutex_exec);

    return NULL;
}

// Lanza el timer de quantum capturando la generación de despacho actual.
static void lanzar_quantum_timer(t_proceso* proc) {
    t_quantum_args* qa = malloc(sizeof(t_quantum_args));
    qa->pid = proc->PID;
    qa->gen = proc->gen_despacho;
    pthread_t t;
    pthread_create(&t, NULL, thread_quantum_timer, qa);
    pthread_detach(t);
}

static void* thread_planificador(void* _) {
    (void)_;
    while (1) {
        sem_wait(&sem_cpu_disponible);
        // Bloquea mientras haya una compactación en curso; se libera al terminar.
        sem_wait(&sem_planificador_ok);
        sem_post(&sem_planificador_ok);

        // Seleccionar el proceso de la cola no vacía de mayor prioridad (índice 0 = mayor)
        t_proceso* proc = NULL;
        int nivel_elegido = 0;
        for (int i = 0; i < n_colas; i++) {
            pthread_mutex_lock(&mutex_colas_ready[i]);
            if (queue_size(colas_ready[i]) > 0) {
                proc = queue_pop(colas_ready[i]);
                nivel_elegido = i;
            }
            pthread_mutex_unlock(&mutex_colas_ready[i]);
            if (proc) break;
        }

        if (!proc) continue;

        pthread_mutex_lock(&mutex_cpus);
        t_cpu_entry* cpu = NULL;
        int cant_cpus = list_size(lista_cpus);
        for (int i = 0; i < cant_cpus; i++) {
            t_cpu_entry* e = list_get(lista_cpus, i);
            if (!e->ocupada) { cpu = e; break; }
        }
        pthread_mutex_unlock(&mutex_cpus);

        log_debug(logger, "Planificador: proceso (%d) nivel=%d -> %s (de %d CPUs conectadas)",
                  proc->PID, nivel_elegido, cpu ? "CPU libre encontrada" : "sin CPU libre", cant_cpus);

        if (!cpu) {
            // No hay CPU libre: reinsertar al FRENTE de la cola de la que salió,
            // para no alterar el orden (en particular, los desalojados por
            // compactación deben conservar su lugar al principio de READY).
            pthread_mutex_lock(&mutex_colas_ready[nivel_elegido]);
            list_add_in_index(colas_ready[nivel_elegido]->elements, 0, proc);
            pthread_mutex_unlock(&mutex_colas_ready[nivel_elegido]);
            continue;
        }

        despachar(proc, cpu);

        if (strcmp(algoritmos_cola[nivel_elegido], "RR") == 0)
            lanzar_quantum_timer(proc);
    }
    return NULL;
}


static t_proceso* sacar_de_exec(int pid) {
    pthread_mutex_lock(&mutex_exec);
    t_proceso* proc = NULL;
    int sz = queue_size(cola_exec);
    for (int i = 0; i < sz; i++) {
        t_proceso* q = queue_pop(cola_exec);
        if (q->PID == pid) proc = q;
        else queue_push(cola_exec, q);
    }
    pthread_mutex_unlock(&mutex_exec);

    if (proc) {
        pthread_mutex_lock(&mutex_cpus);
        for (int i = 0; i < list_size(lista_cpus); i++) {
            t_cpu_entry* e = list_get(lista_cpus, i);
            if (e->fd == proc->fd_cpu) { e->ocupada = 0; break; }
        }
        pthread_mutex_unlock(&mutex_cpus);
        proc->fd_cpu = -1;

        // Contabilidad de compactación: si el proceso fue marcado para desalojo,
        // avisar que su CPU ya lo devolvió — sea cual sea la vía por la que salió
        // de EXEC (interrupción, syscall bloqueante, EXIT o error). Si solo se
        // contara la vía de interrupción, un proceso que justo hizo una syscall
        // dejaría a handle_compactar esperando para siempre.
        pthread_mutex_lock(&mutex_compactando);
        if (compactando && proc->preemptado) sem_post(&sem_cpus_devueltas);
        pthread_mutex_unlock(&mutex_compactando);
    }
    return proc;
}

// Mueve el proceso a BLOCK y señala al planificador que la CPU quedó libre.
static void mover_a_block(t_proceso* proc) {
    proc->preemptado = 0; // si estaba marcado para desalojo, sacar_de_exec ya lo contabilizó
    proc->gen_bloqueo++;
    cambiar_estado(proc, BLOCK);
    pthread_mutex_lock(&mutex_block);
    queue_push(cola_block, proc);
    pthread_mutex_unlock(&mutex_block);
    sem_post(&sem_cpu_disponible);

    t_suspension_args* args = malloc(sizeof(t_suspension_args));
    if (!args) {
        log_error(logger, "## (%d) - No se pudo crear el timer de suspensión", proc->PID);
        return;
    }
    args->pid          = proc->PID;
    args->gen_bloqueo = proc->gen_bloqueo;
    pthread_t t;
    if (pthread_create(&t, NULL, thread_suspension_timer, args) == 0) {
        pthread_detach(t);
    } else {
        log_error(logger, "## (%d) - No se pudo iniciar el timer de suspensión", proc->PID);
        free(args);
    }
}

// Saca el proceso de cola_block por PID. Retorna el proceso o NULL.
static t_proceso* sacar_de_block(int pid) {
    pthread_mutex_lock(&mutex_block);
    t_proceso* proc = NULL;
    int sz = queue_size(cola_block);
    for (int i = 0; i < sz; i++) {
        t_proceso* q = queue_pop(cola_block);
        if (q->PID == pid) proc = q;
        else queue_push(cola_block, q);
    }
    pthread_mutex_unlock(&mutex_block);
    return proc;
}

// Variante usada por el timer: solo extrae el bloqueo que originó ESE timer.
// Si el PID ya completó otra IO y volvió a bloquearse, la generación no coincide.
static t_proceso* sacar_de_block_generacion(int pid, int gen_bloqueo) {
    pthread_mutex_lock(&mutex_block);
    t_proceso* proc = NULL;
    int sz = queue_size(cola_block);
    for (int i = 0; i < sz; i++) {
        t_proceso* q = queue_pop(cola_block);
        if (!proc && q->PID == pid && q->gen_bloqueo == gen_bloqueo) proc = q;
        else queue_push(cola_block, q);
    }
    pthread_mutex_unlock(&mutex_block);
    return proc;
}


static t_proceso* sacar_de_susp_block(int pid) {
    pthread_mutex_lock(&mutex_susp_block);
    t_proceso* proc = NULL;
    int sz = queue_size(cola_susp_block);
    for (int i = 0; i < sz; i++) {
        t_proceso* q = queue_pop(cola_susp_block);
        if (q->PID == pid) proc = q;
        else queue_push(cola_susp_block, q);
    }
    pthread_mutex_unlock(&mutex_susp_block);
    return proc;
}


// Busca un proceso por PID en exec, en las colas de ready y en block, sin
// sacarlo de ninguna cola. Retorna puntero al t_proceso o NULL.
// Usado para herencia de prioridades: el owner de un mutex puede estar en
// BLOCK (p.ej. hizo MUTEX_LOCK y después SLEEP sin soltar el mutex) al
// momento de tener que elevarle la prioridad, no solo en EXEC/READY.
static t_proceso* buscar_proceso_activo(int pid) {
    pthread_mutex_lock(&mutex_exec);
    t_proceso* p = buscar_pid_en_queue(cola_exec, pid);
    pthread_mutex_unlock(&mutex_exec);
    if (p) return p;

    for (int nivel = 0; nivel < n_colas; nivel++) {
        pthread_mutex_lock(&mutex_colas_ready[nivel]);
        p = buscar_pid_en_queue(colas_ready[nivel], pid);
        pthread_mutex_unlock(&mutex_colas_ready[nivel]);
        if (p) return p;
    }

    pthread_mutex_lock(&mutex_block);
    p = buscar_pid_en_queue(cola_block, pid);
    pthread_mutex_unlock(&mutex_block);
    return p;
}

static void* thread_suspension_timer(void* arg) {
    t_suspension_args args = *((t_suspension_args*)arg);
    free(arg);

    struct timespec ts = {
        .tv_sec  = suspension_timeout_ms / 1000,
        .tv_nsec = (long)(suspension_timeout_ms % 1000) * 1000000L
    };
    nanosleep(&ts, NULL);

    pthread_mutex_lock(&mutex_transiciones_io);
    t_proceso* proc = sacar_de_block_generacion(args.pid, args.gen_bloqueo);
    if (!proc) {
        pthread_mutex_unlock(&mutex_transiciones_io);
        return NULL;
    }

    log_info(logger, "## (%d) - Timeout de IO: suspendiendo proceso", args.pid);
    uint32_t pid_n = htonl((uint32_t)args.pid);
    t_mensaje* resp = km_request(MSG_SUSPENDER_PROCESO, &pid_n, sizeof(uint32_t));
    if (resp && resp->op_code == MSG_OK) {
        // El estado se publica recién después de que KM confirma que los
        // segmentos ya fueron enviados a Swap.
        proc->tiempo_suspension = time(NULL);
        cambiar_estado(proc, SUSP_BLOCK);
        pthread_mutex_lock(&mutex_susp_block);
        queue_push(cola_susp_block, proc);
        pthread_mutex_unlock(&mutex_susp_block);
        // Sus segmentos liberaron memoria física; puede habilitar la
        // des-suspensión de otro proceso que ya estaba en SUSP_READY.
        sem_post(&sem_largo_plazo);
    } else {
        // KM no lo suspendió: sigue residente y debe continuar en BLOCK para
        // que la finalización de la IO todavía pueda encontrarlo.
        log_error(logger, "KM rechazó suspender proceso %d; permanece en BLOCK", args.pid);
        pthread_mutex_lock(&mutex_block);
        queue_push(cola_block, proc);
        pthread_mutex_unlock(&mutex_block);
    }
    if (resp) free_mensaje(resp);
    pthread_mutex_unlock(&mutex_transiciones_io);

    return NULL;
}

static void* thread_largo_plazo(void* _) {
    (void)_;
    while (1) {
        sem_wait(&sem_largo_plazo);
        manejar_mas_memoria(NULL);
    }
    return NULL;
}

// Quita el proceso de cola_exec pero NO marca la CPU como libre.
// La CPU sigue ocupada esperando la respuesta de memoria — el planificador
// no debe despacharle otro proceso mientras tanto.
static t_proceso* sacar_proc_de_exec_para_mem(int pid) {
    pthread_mutex_lock(&mutex_exec);
    t_proceso* proc = NULL;
    int sz = queue_size(cola_exec);
    for (int i = 0; i < sz; i++) {
        t_proceso* q = queue_pop(cola_exec);
        if (q->PID == pid) proc = q;
        else               queue_push(cola_exec, q);
    }
    pthread_mutex_unlock(&mutex_exec);
    // NO tocamos cpu.ocupada ni proc->fd_cpu: la CPU permanece "ocupada"

    if (proc) {
        // Contabilidad de compactación: si estaba marcado para desalojo pero su
        // CPU alcanzó a enviar la syscall de memoria antes de la interrupción,
        // contarlo como devuelto (la CPU queda ociosa esperando el redespacho).
        pthread_mutex_lock(&mutex_compactando);
        if (compactando && proc->preemptado) sem_post(&sem_cpus_devueltas);
        pthread_mutex_unlock(&mutex_compactando);
        proc->preemptado = 0;
    }
    return proc;
}

// Re-establece el proceso en cola_exec sin re-despachar.
static void re_exec_sin_despachar(t_proceso* proc) {
    pthread_mutex_lock(&mutex_exec);
    queue_push(cola_exec, proc);
    pthread_mutex_unlock(&mutex_exec);
}

// Tras una syscall de memoria la CPU devolvió el proceso (motivo SYSCALL) y
// quedó ociosa esperando un MSG_DESPACHAR_PROCESO, con su entrada aún marcada
// como ocupada para que el planificador no le asigne otro proceso. Cumplido el
// pedido, se le reenvía el MISMO proceso (requisito del enunciado): la CPU
// restaura el contexto desde KM — con la tabla de segmentos ya actualizada —
// y continúa desde el PC guardado.
static void redespachar_a_misma_cpu(t_proceso* proc) {
    int fd_cpu = proc->fd_cpu;
    proc->gen_despacho++;
    re_exec_sin_despachar(proc);

    t_payload_despachar_proceso payload = { .pid = htonl(proc->PID) };
    enviar_mensaje(fd_cpu, MSG_DESPACHAR_PROCESO, &payload, sizeof(payload));

    int nivel = (proc->prioridad >= 0 && proc->prioridad < n_colas) ? proc->prioridad : n_colas - 1;
    if (strcmp(algoritmos_cola[nivel], "RR") == 0)
        lanzar_quantum_timer(proc);
}

// MEM_ALLOC falló (sin espacio incluso tras compactar): el proceso no puede
// continuar. Se lo finaliza y se libera la CPU que había quedado reservada.
static void finalizar_por_error_mem(t_proceso* proc) {
    int fd_cpu = proc->fd_cpu;
    proc->fd_cpu = -1;

    pthread_mutex_lock(&mutex_cpus);
    for (int i = 0; i < list_size(lista_cpus); i++) {
        t_cpu_entry* e = list_get(lista_cpus, i);
        if (e->fd == fd_cpu) { e->ocupada = 0; break; }
    }
    pthread_mutex_unlock(&mutex_cpus);

    cambiar_estado(proc, EXIT);
    log_info(logger, "## (%d) finalizó su ejecución con motivo de ERROR", proc->PID);
    pthread_mutex_lock(&mutex_exit);
    queue_push(cola_exit, proc);
    pthread_mutex_unlock(&mutex_exit);
    avisar_km_finalizacion(proc->PID);
    sem_post(&sem_cpu_disponible);
}

typedef struct {
    t_proceso* proc;
    uint32_t   id_segmento;
    uint32_t   tamanio;
} t_args_mem_alloc;

typedef struct {
    t_proceso* proc;
    uint32_t   id_segmento;
} t_args_mem_free;

static void* manejar_mem_alloc(void* arg) {
    t_args_mem_alloc* a = (t_args_mem_alloc*) arg;

    t_payload_crear_segmento payload = {
        .pid         = htonl((uint32_t)a->proc->PID),
        .id_segmento = htonl(a->id_segmento),
        .tamanio     = htonl(a->tamanio)
    };

    log_info(logger, "## (%d) - MEM_ALLOC: solicita crear segmento %u (%u bytes)",
             a->proc->PID, a->id_segmento, a->tamanio);

    t_mensaje* resp = km_request(MSG_CREAR_SEGMENTO, &payload, sizeof(payload));

    // Si este pedido disparó una compactación, KM responde recién al terminarla:
    // despertar a handle_compactar para que reanude el planificador.
    pthread_mutex_lock(&mutex_compactando);
    if (esperando_fin_compactacion) {
        esperando_fin_compactacion = 0;
        sem_post(&sem_fin_compactacion);
    }
    pthread_mutex_unlock(&mutex_compactando);

    if (resp && resp->op_code == MSG_TABLA_SEGMENTOS) {
        log_info(logger, "## (%d) - MEM_ALLOC: segmento %u creado exitosamente",
                 a->proc->PID, a->id_segmento);
        redespachar_a_misma_cpu(a->proc);
    } else {
        log_error(logger, "## (%d) - MEM_ALLOC: KM no pudo crear segmento %u",
                  a->proc->PID, a->id_segmento);
        finalizar_por_error_mem(a->proc);
    }

    if (resp) free_mensaje(resp);
    free(a);
    return NULL;
}

static void* manejar_mem_free(void* arg) {
    t_args_mem_free* a = (t_args_mem_free*) arg;

    t_payload_eliminar_segmento payload = {
        .pid         = htonl((uint32_t)a->proc->PID),
        .id_segmento = htonl(a->id_segmento)
    };

    log_info(logger, "## (%d) - MEM_FREE: solicita eliminar segmento %u",
             a->proc->PID, a->id_segmento);

    t_mensaje* resp = km_request(MSG_ELIMINAR_SEGMENTO, &payload, sizeof(payload));

    if (resp && resp->op_code == MSG_OK) {
        log_info(logger, "## (%d) - MEM_FREE: segmento %u eliminado",
                 a->proc->PID, a->id_segmento);
        sem_post(&sem_largo_plazo);
    } else {
        log_error(logger, "## (%d) - MEM_FREE: KM reportó error eliminando segmento %u",
                  a->proc->PID, a->id_segmento);
    }
    // El proceso continúa en la misma CPU independientemente del resultado de KM.
    redespachar_a_misma_cpu(a->proc);

    if (resp) free_mensaje(resp);
    free(a);
    return NULL;
}

// La CPU envía [SYSCALL][DEVOLVER_PROCESO(SYSCALL)] en ese orden y nada en el
// medio. Los handlers que gestionan ellos mismos el destino del proceso
// (SLEEP/STDOUT/STDIN/MUTEX_LOCK/MEM_*) consumen acá el DEVOLVER: si lo
// procesara el switch genérico después de que el proceso fue re-planificado,
// lo sacaría de EXEC por error y se despacharía dos veces.
static void consumir_devolver(int fd, const char* syscall_nombre) {
    t_mensaje* dev = recibir_mensaje(fd);
    if (dev) {
        if (dev->op_code != MSG_DEVOLVER_PROCESO)
            log_warning(logger, "## %s: se esperaba DEVOLVER_PROCESO, llegó op %u",
                        syscall_nombre, dev->op_code);
        free_mensaje(dev);
    }
}

static void atender_cpu(int fd, t_cpu_entry* entry) {
    while (1) {
        t_mensaje* msg = recibir_mensaje(fd);
        if (!msg) {
            // Sacar la CPU de la lista para que el planificador no le despache
            // procesos a un socket muerto. No se libera 'entry': el planificador
            // podría estar reteniendo el puntero en este mismo instante (fuga
            // mínima y deliberada, una entrada por desconexión).
            pthread_mutex_lock(&mutex_cpus);
            list_remove_element(lista_cpus, entry);
            pthread_mutex_unlock(&mutex_cpus);

            // Rescatar el proceso que estuviera ejecutando en esta CPU: vuelve
            // a READY y re-ejecutará desde su último contexto guardado en KM.
            pthread_mutex_lock(&mutex_exec);
            t_proceso* huerfano = NULL;
            int sz_exec = queue_size(cola_exec);
            for (int i = 0; i < sz_exec; i++) {
                t_proceso* q = queue_pop(cola_exec);
                if (!huerfano && q->fd_cpu == fd) huerfano = q;
                else queue_push(cola_exec, q);
            }
            pthread_mutex_unlock(&mutex_exec);

            if (huerfano) {
                huerfano->fd_cpu = -1;
                pthread_mutex_lock(&mutex_compactando);
                if (compactando && huerfano->preemptado) sem_post(&sem_cpus_devueltas);
                pthread_mutex_unlock(&mutex_compactando);
                huerfano->preemptado = 0;
                log_warning(logger, "## (%d) CPU desconectada durante EXEC — vuelve a READY", huerfano->PID);
                cambiar_estado(huerfano, READY);
                encolar_en_ready(huerfano);
            }

            log_warning(logger, "## CPU fd=%d desconectada", fd);
            return;
        }

        switch (msg->op_code) {

        case MSG_DEVOLVER_PROCESO: {
            t_payload_devolver_proceso* p = msg->payload;
            int pid    = (int)ntohl(p->pid);
            int motivo = (int)ntohl(p->motivo);

            t_proceso* proc = sacar_de_exec(pid);
            if (!proc) break;

            if (motivo == MOTIVO_DEVOLUCION_EXIT) {
                cambiar_estado(proc, EXIT);
                log_info(logger, "## (%d) finalizó su ejecución con motivo de EXIT", pid);
                pthread_mutex_lock(&mutex_exit);
                queue_push(cola_exit, proc);
                pthread_mutex_unlock(&mutex_exit);
                avisar_km_finalizacion(pid);
                sem_post(&sem_cpu_disponible);

            } else if (motivo == MOTIVO_DEVOLUCION_ERROR) {
                cambiar_estado(proc, EXIT);
                log_info(logger, "## (%d) finalizó su ejecución con motivo de ERROR", pid);
                pthread_mutex_lock(&mutex_exit);
                queue_push(cola_exit, proc);
                pthread_mutex_unlock(&mutex_exit);
                avisar_km_finalizacion(pid);
                sem_post(&sem_cpu_disponible);

            } else if (motivo == MOTIVO_DEVOLUCION_SEG_FAULT) {
                cambiar_estado(proc, EXIT);
                log_info(logger, "## (%d) finalizó su ejecución con motivo de SEG_FAULT", pid);
                pthread_mutex_lock(&mutex_exit);
                queue_push(cola_exit, proc);
                pthread_mutex_unlock(&mutex_exit);
                avisar_km_finalizacion(pid);
                sem_post(&sem_cpu_disponible);

            } else if (motivo == MOTIVO_DEVOLUCION_INTERRUPCION) {
                cambiar_estado(proc, READY);
                if (proc->preemptado) {
                    // La contabilidad de compactación (sem_cpus_devueltas) ya la
                    // hizo sacar_de_exec; acá solo se conserva el lugar al frente.
                    proc->preemptado = 0;
                    encolar_al_frente_en_ready(proc);
                } else {
                    log_info(logger, "## (%d) - Desalojado por fin de quantum", pid);
                    encolar_en_ready(proc);
                }

            } else {
                // MOTIVO_DEVOLUCION_SYSCALL: syscall no bloqueante (MUTEX_CREATE, MUTEX_UNLOCK, MUTEX_LOCK libre)
                proc->preemptado = 0;
                cambiar_estado(proc, READY);
                encolar_en_ready(proc);
            }
            break;
        }

        case MSG_SYSCALL_SLEEP: {
            if (msg->payload_size < sizeof(t_payload_io_sleep)) {
                log_error(logger, "CPU envió SLEEP con payload inválido");
                break;
            }
            t_payload_io_sleep* p = msg->payload;
            int      pid       = (int)ntohl(p->pid);
            uint32_t tiempo_ms = ntohl(p->tiempo_ms);

            log_info(logger, "## (%d) - Solicitó syscall: SLEEP", pid);
            consumir_devolver(fd, "SLEEP");

            pthread_mutex_lock(&mutex_transiciones_io);
            t_proceso* proc = sacar_de_exec(pid);
            if (proc) mover_a_block(proc);

            pthread_mutex_lock(&mutex_io);
            int fd_io = fd_io_sleep;
            if (proc && fd_io != -1) {
                t_payload_io_sleep fwd = { .pid = htonl(pid), .tiempo_ms = htonl(tiempo_ms) };
                enviar_mensaje(fd_io, MSG_IO_SLEEP, &fwd, sizeof(fwd));
            }
            pthread_mutex_unlock(&mutex_io);

            if (proc && fd_io == -1) {
                t_proceso* bloqueado = sacar_de_block(pid);
                if (bloqueado)
                    finalizar_por_error_io(bloqueado, "interfaz SLEEP no conectada");
            }
            pthread_mutex_unlock(&mutex_transiciones_io);
            break;
        }

        case MSG_SYSCALL_STDOUT: {
            if (msg->payload_size < sizeof(t_payload_syscall_io_memoria)) {
                log_error(logger, "CPU envió STDOUT con payload inválido");
                break;
            }
            t_payload_syscall_io_memoria* p = msg->payload;
            int      pid        = (int)ntohl(p->pid);
            uint32_t dir_logica = ntohl(p->direccion_logica);
            uint32_t tamanio    = ntohl(p->tamanio);
            log_info(logger, "## (%d) - Solicitó syscall: STDOUT", pid);
            consumir_devolver(fd, "STDOUT");

            pthread_mutex_lock(&mutex_transiciones_io);
            t_proceso* proc = sacar_de_exec(pid);
            if (proc) mover_a_block(proc);

            pthread_mutex_lock(&mutex_io);
            int fd_io = fd_io_stdout;
            pthread_mutex_unlock(&mutex_io);

            bool io_iniciada = false;
            if (proc && fd_io != -1) {
                t_payload_acceso_datos km_payload = {
                    .pid        = htonl((uint32_t)pid),
                    .dir_logica = htonl(dir_logica),
                    .tamanio    = htonl(tamanio)
                };
                t_mensaje* resp = km_request(MSG_LEER_DATOS, &km_payload, sizeof(km_payload));
                if (resp && resp->op_code == MSG_LEER_DATOS_RESP &&
                    resp->payload_size == tamanio &&
                    resp->payload_size <= UINT32_MAX - sizeof(uint32_t)) {
                    uint32_t total = sizeof(uint32_t) + resp->payload_size;
                    uint8_t* buf = malloc(total);
                    if (buf) {
                        uint32_t pid_n = htonl((uint32_t)pid);
                        memcpy(buf, &pid_n, sizeof(uint32_t));
                        memcpy(buf + sizeof(uint32_t), resp->payload, resp->payload_size);

                        // mutex_io cubre el envío completo para impedir que
                        // mensajes concurrentes se intercalen en el socket.
                        pthread_mutex_lock(&mutex_io);
                        if (fd_io_stdout == fd_io) {
                            enviar_mensaje(fd_io, MSG_IO_STDOUT, buf, total);
                            io_iniciada = true;
                        }
                        pthread_mutex_unlock(&mutex_io);
                        free(buf);
                    }
                }
                if (!io_iniciada) {
                    log_warning(logger, "## (%d) - STDOUT: error al leer datos de KM", pid);
                }
                if (resp) free_mensaje(resp);
            }

            if (proc && !io_iniciada) {
                t_proceso* bloqueado = sacar_de_block(pid);
                if (bloqueado)
                    finalizar_por_error_io(bloqueado,
                        fd_io == -1 ? "interfaz STDOUT no conectada" :
                                      "no se pudo iniciar la operación STDOUT");
            }
            pthread_mutex_unlock(&mutex_transiciones_io);
            break;
        }

        case MSG_SYSCALL_STDIN: {
            if (msg->payload_size < sizeof(t_payload_syscall_io_memoria)) {
                log_error(logger, "CPU envió STDIN con payload inválido");
                break;
            }
            t_payload_syscall_io_memoria* p = msg->payload;
            int      pid        = (int)ntohl(p->pid);
            uint32_t dir_logica = ntohl(p->direccion_logica);
            uint32_t n_bytes    = ntohl(p->tamanio);
            log_info(logger, "## (%d) - Solicitó syscall: STDIN", pid);
            consumir_devolver(fd, "STDIN");

            pthread_mutex_lock(&mutex_transiciones_io);
            t_proceso* proc = sacar_de_exec(pid);
            if (proc) mover_a_block(proc);

            t_stdin_pendiente* sp = proc ? calloc(1, sizeof(t_stdin_pendiente)) : NULL;
            if (sp) {
                sp->pid        = pid;
                sp->dir_logica = dir_logica;
                sp->tamanio    = n_bytes;
                proc->esperando_stdin = 1;
                pthread_mutex_lock(&mutex_stdin_pendientes);
                list_add(lista_stdin_pendientes, sp);
                pthread_mutex_unlock(&mutex_stdin_pendientes);
            }

            pthread_mutex_lock(&mutex_io);
            int fd_io = fd_io_stdin;
            if (sp && fd_io != -1) {
                t_payload_io_stdin fwd = { .pid = htonl(pid), .n_bytes = htonl(n_bytes) };
                enviar_mensaje(fd_io, MSG_IO_STDIN, &fwd, sizeof(fwd));
            }
            pthread_mutex_unlock(&mutex_io);

            if (proc && (!sp || fd_io == -1)) {
                t_stdin_pendiente* pendiente = extraer_stdin_pendiente(pid);
                destruir_stdin_pendiente(pendiente);
                proc->esperando_stdin = 0;
                t_proceso* bloqueado = sacar_de_block(pid);
                if (bloqueado)
                    finalizar_por_error_io(bloqueado,
                        !sp ? "sin memoria para registrar STDIN" :
                              "interfaz STDIN no conectada");
            }
            pthread_mutex_unlock(&mutex_transiciones_io);
            break;
        }

        case MSG_MUTEX_CREATE: {
            t_payload_mutex* p = msg->payload;
            int pid = (int)ntohl(p->pid);
            log_info(logger, "## (%d) - Solicitó syscall: MUTEX_CREATE", pid);
            mutex_ks_create(p->nombre);
            // No se envía MSG_OK: CPU ya devolvió el proceso, KS re-despacha
            // por el planificador vía MSG_DEVOLVER_PROCESO.
            break;
        }
        case MSG_MUTEX_LOCK: {
            t_payload_mutex* p = msg->payload;
            int pid = (int)ntohl(p->pid);
            log_info(logger, "## (%d) - Solicitó syscall: MUTEX_LOCK", pid);
            consumir_devolver(fd, "MUTEX_LOCK");

            t_proceso* proc = sacar_de_exec(pid);
            if (!proc) break;

            int owner_a_elevar, nueva_prioridad_owner;
            int resultado = mutex_ks_lock(pid, proc->prioridad, p->nombre, logger,
                                          &owner_a_elevar, &nueva_prioridad_owner);
            proc->preemptado = 0; // sacar_de_exec ya contabilizó la compactación
            if (resultado == 0) {
                cambiar_estado(proc, READY);
                encolar_en_ready(proc);
            } else if (resultado == 1) {
                // Herencia de prioridades: elevar al owner si corresponde.
                if (owner_a_elevar >= 0) {
                    t_proceso* owner = buscar_proceso_activo(owner_a_elevar);
                    if (owner && owner->prioridad > nueva_prioridad_owner) {
                        log_info(logger, "## %d Cambio de prioridad: %d - %d",
                                 owner->PID, owner->prioridad, nueva_prioridad_owner);
                        owner->prioridad = nueva_prioridad_owner;
                    }
                }
                mover_a_block(proc);
            } else {
                // Mutex inexistente (LOCK sin CREATE): el proceso ya salió de
                // EXEC — finalizarlo en lugar de dejarlo fuera de todas las colas.
                log_error(logger, "## (%d) MUTEX_LOCK sobre mutex inexistente '%s'", pid, p->nombre);
                cambiar_estado(proc, EXIT);
                log_info(logger, "## (%d) finalizó su ejecución con motivo de ERROR", pid);
                pthread_mutex_lock(&mutex_exit);
                queue_push(cola_exit, proc);
                pthread_mutex_unlock(&mutex_exit);
                avisar_km_finalizacion(pid);
                sem_post(&sem_cpu_disponible);
            }
            break;
        }
        case MSG_MEM_ALLOC: {
            // Nota: no se hace free_mensaje acá — el free único está al final
            // del loop (hacerlo en ambos lados era un doble free que abortaba
            // el módulo). Tampoco se envía nada a la CPU: quedó esperando un
            // MSG_DESPACHAR_PROCESO y cualquier otro mensaje la confundiría.
            if (msg->payload_size < sizeof(t_payload_syscall_mem_alloc)) {
                log_error(logger, "CPU envio MEM_ALLOC con payload invalido");
                break;
            }
            t_payload_syscall_mem_alloc* p = (t_payload_syscall_mem_alloc*) msg->payload;
            int pid_ma = (int)ntohl(p->pid);
            log_info(logger, "## (%d) - Solicitó syscall: MEM_ALLOC", pid_ma);

            t_proceso* proc_ma = sacar_proc_de_exec_para_mem(pid_ma);
            if (!proc_ma) {
                log_error(logger, "## MEM_ALLOC: proceso %d no encontrado en EXEC", pid_ma);
                break;
            }

            consumir_devolver(fd, "MEM_ALLOC");

            t_args_mem_alloc* args_ma = malloc(sizeof(t_args_mem_alloc));
            args_ma->proc        = proc_ma;
            args_ma->id_segmento = ntohl(p->id_segmento);
            args_ma->tamanio     = ntohl(p->tamanio);

            pthread_t t_ma;
            pthread_create(&t_ma, NULL, manejar_mem_alloc, args_ma);
            pthread_detach(t_ma);
            break;
        }
        case MSG_MEM_FREE: {
            if (msg->payload_size < sizeof(t_payload_syscall_mem_free)) {
                log_error(logger, "CPU envio MEM_FREE con payload invalido");
                break;
            }
            t_payload_syscall_mem_free* p = (t_payload_syscall_mem_free*) msg->payload;
            int pid_mf = (int)ntohl(p->pid);
            log_info(logger, "## (%d) - Solicitó syscall: MEM_FREE", pid_mf);

            t_proceso* proc_mf = sacar_proc_de_exec_para_mem(pid_mf);
            if (!proc_mf) {
                log_error(logger, "## MEM_FREE: proceso %d no encontrado en EXEC", pid_mf);
                break;
            }

            consumir_devolver(fd, "MEM_FREE");

            t_args_mem_free* args_mf = malloc(sizeof(t_args_mem_free));
            args_mf->proc        = proc_mf;
            args_mf->id_segmento = ntohl(p->id_segmento);

            pthread_t t_mf;
            pthread_create(&t_mf, NULL, manejar_mem_free, args_mf);
            pthread_detach(t_mf);
            break;
        }
        case MSG_MUTEX_UNLOCK: {
            t_payload_mutex* p = msg->payload;
            int pid = (int)ntohl(p->pid);
            log_info(logger, "## (%d) - Solicitó syscall: MUTEX_UNLOCK", pid);

            // No se envía MSG_OK: CPU ya devolvió el proceso, KS re-despacha
            // por el planificador vía MSG_DEVOLVER_PROCESO.
            int prioridad_restaurar;
            int waiter_pid = mutex_ks_unlock(pid, p->nombre, logger, &prioridad_restaurar);

            // Restaurar la prioridad original del owner si fue elevada por herencia.
            if (prioridad_restaurar >= 0) {
                t_proceso* owner = buscar_proceso_activo(pid);
                if (owner && owner->prioridad != prioridad_restaurar) {
                    log_info(logger, "## %d Cambio de prioridad: %d - %d",
                             owner->PID, owner->prioridad, prioridad_restaurar);
                    owner->prioridad = prioridad_restaurar;
                }
            }

            if (waiter_pid >= 0) {
                // Mover al waiter de BLOCK → READY (o SUSP_BLOCK → SUSP_READY)
                pthread_mutex_lock(&mutex_transiciones_io);
                t_proceso* waiter = sacar_de_block(waiter_pid);
                if (waiter) {
                    cambiar_estado(waiter, READY);
                    encolar_en_ready(waiter);
                } else {
                    waiter = sacar_de_susp_block(waiter_pid);
                    if (waiter) {
                        cambiar_estado(waiter, SUSP_READY);
                        pthread_mutex_lock(&mutex_susp_ready);
                        queue_push(cola_susp_ready, waiter);
                        pthread_mutex_unlock(&mutex_susp_ready);
                        sem_post(&sem_largo_plazo);
                    }
                }
                pthread_mutex_unlock(&mutex_transiciones_io);
            }
            break;
        }

        case MSG_SYSCALL_EXIT: {
            t_payload_syscall_exit* p = msg->payload;
            int pid = (int)ntohl(p->pid);

            log_info(logger, "## (%d) - Solicitó syscall: EXIT", pid);

            t_proceso* proc = sacar_de_exec(pid);
            if (proc) {
                cambiar_estado(proc, EXIT);
                log_info(logger, "## (%d) finalizó su ejecución con motivo de EXIT", pid);
                pthread_mutex_lock(&mutex_exit);
                queue_push(cola_exit, proc);
                pthread_mutex_unlock(&mutex_exit);
                avisar_km_finalizacion(pid);
                sem_post(&sem_cpu_disponible);
            }
            // No se responde MSG_OK: la CPU no lo espera (tras el EXIT devuelve
            // el proceso y queda esperando un MSG_DESPACHAR_PROCESO; un OK acá
            // la hacía fallar en recibir_proceso_a_ejecutar).
            break;
        }

        case MSG_INIT_PROC: {
            if (msg->payload_size < sizeof(uint32_t)) {
                enviar_mensaje(fd, MSG_ERROR, NULL, 0);
                break;
            }
            // Layout del payload: { uint32_t pid_padre, char path[], uint32_t prioridad }
            uint32_t pid_padre_n;
            memcpy(&pid_padre_n, msg->payload, sizeof(uint32_t));
            int pid_padre = (int)ntohl(pid_padre_n);

            char* path_hijo = (char*)msg->payload + sizeof(uint32_t);
            size_t path_len = strlen(path_hijo) + 1;
            uint32_t prioridad_n;
            memcpy(&prioridad_n,
                   (char*)msg->payload + sizeof(uint32_t) + path_len,
                   sizeof(uint32_t));
            int prioridad = (int)ntohl(prioridad_n);

            log_info(logger, "## (%d) - Solicitó syscall: INIT_PROC", pid_padre);
            t_proceso* hijo = crear_proceso(path_hijo, prioridad);
            enviar_mensaje(fd, hijo ? MSG_OK : MSG_ERROR, NULL, 0);
            break;
        }

        default:
            log_warning(logger, "## KS: op_code desconocido %u", msg->op_code);
            break;
        }

        free_mensaje(msg);
    }
}


static void atender_io(int fd, char* tipo) {
    while (1) {
        t_mensaje* msg = recibir_mensaje(fd);
        if (!msg) {
            // Invalidar el fd para que las próximas syscalls no se envíen a un
            // socket muerto (y una eventual reconexión de la IO lo reemplace).
            pthread_mutex_lock(&mutex_io);
            if      (fd_io_sleep  == fd) fd_io_sleep  = -1;
            else if (fd_io_stdout == fd) fd_io_stdout = -1;
            else if (fd_io_stdin  == fd) fd_io_stdin  = -1;
            pthread_mutex_unlock(&mutex_io);
            log_warning(logger, "## IO %s desconectada", tipo);
            return;
        }

        if (msg->op_code == MSG_IO_FIN) {
            if (msg->payload_size < sizeof(t_payload_io_fin)) {
                log_warning(logger, "## IO %s envió MSG_IO_FIN con payload inválido", tipo);
                free_mensaje(msg);
                continue;
            }
            t_payload_io_fin* p = msg->payload;
            int pid = (int)ntohl(p->pid);

            pthread_mutex_lock(&mutex_transiciones_io);
            t_proceso* proc = sacar_de_block(pid);
            if (proc) {
                if (proc->esperando_stdin) {
                    t_stdin_pendiente* sp = extraer_stdin_pendiente(pid);
                    destruir_stdin_pendiente(sp);
                    proc->esperando_stdin = 0;
                    finalizar_por_error_io(proc,
                        "STDIN respondió MSG_IO_FIN sin entregar sus datos");
                } else {
                    // IO terminó antes del timeout: BLOCK → READY
                    cambiar_estado(proc, READY);
                    log_info(logger, "## (%d) finalizó IO y pasa a READY", pid);
                    encolar_en_ready(proc);
                }
            } else {
                // IO terminó después del timeout: SUSP. BLOCK → SUSP. READY
                proc = sacar_de_susp_block(pid);
                if (proc) {
                    if (proc->esperando_stdin) {
                        t_stdin_pendiente* sp = extraer_stdin_pendiente(pid);
                        destruir_stdin_pendiente(sp);
                        proc->esperando_stdin = 0;
                        finalizar_por_error_io(proc,
                            "STDIN respondió MSG_IO_FIN sin entregar sus datos");
                    } else {
                        cambiar_estado(proc, SUSP_READY);
                        log_info(logger, "## (%d) finalizó IO y pasa a SUSP. READY", pid);
                        pthread_mutex_lock(&mutex_susp_ready);
                        queue_push(cola_susp_ready, proc);
                        pthread_mutex_unlock(&mutex_susp_ready);
                        sem_post(&sem_largo_plazo);
                    }
                }
            }
            pthread_mutex_unlock(&mutex_transiciones_io);

        } else if (msg->op_code == MSG_IO_STDIN_DATOS) {
            if (msg->payload_size < 8) { free_mensaje(msg); continue; }

            uint32_t pid_n, n_bytes_n;
            memcpy(&pid_n,     msg->payload,               4);
            memcpy(&n_bytes_n, (uint8_t*)msg->payload + 4, 4);
            int      pid     = (int)ntohl(pid_n);
            uint32_t n_bytes = ntohl(n_bytes_n);
            uint8_t* datos   = (uint8_t*)msg->payload + 8;

            // Conservar los bytes junto al pedido. Si el proceso está
            // suspendido, se escribirán recién después de restaurar segmentos.
            bool pendiente_encontrado = false;
            bool datos_validos = false;
            pthread_mutex_lock(&mutex_stdin_pendientes);
            for (int i = 0; i < list_size(lista_stdin_pendientes); i++) {
                t_stdin_pendiente* sp = list_get(lista_stdin_pendientes, i);
                if (sp->pid == pid) {
                    pendiente_encontrado = true;
                    uint32_t bytes_en_payload = msg->payload_size - 8u;
                    if (!sp->datos_recibidos &&
                        n_bytes == bytes_en_payload &&
                        n_bytes == sp->tamanio) {
                        uint8_t* copia = n_bytes > 0 ? malloc(n_bytes) : NULL;
                        if (n_bytes == 0 || copia) {
                            if (n_bytes > 0) memcpy(copia, datos, n_bytes);
                            sp->datos           = copia;
                            sp->datos_size      = n_bytes;
                            sp->datos_recibidos = true;
                            datos_validos       = true;
                        }
                    }
                    break;
                }
            }
            pthread_mutex_unlock(&mutex_stdin_pendientes);

            if (!pendiente_encontrado) {
                log_warning(logger, "## (%d) - STDIN: respuesta sin pedido pendiente", pid);
                free_mensaje(msg);
                continue;
            }

            pthread_mutex_lock(&mutex_transiciones_io);
            bool estaba_suspendido = false;
            t_proceso* proc = sacar_de_block(pid);
            if (!proc) {
                proc = sacar_de_susp_block(pid);
                estaba_suspendido = proc != NULL;
            }

            if (!proc) {
                t_stdin_pendiente* sp = extraer_stdin_pendiente(pid);
                destruir_stdin_pendiente(sp);
                log_warning(logger, "## (%d) - STDIN: el proceso ya no esperaba esta IO", pid);
            } else if (!datos_validos) {
                t_stdin_pendiente* sp = extraer_stdin_pendiente(pid);
                destruir_stdin_pendiente(sp);
                proc->esperando_stdin = 0;
                finalizar_por_error_io(proc, "respuesta STDIN inválida o incompleta");
            } else if (estaba_suspendido) {
                // No tocar KM todavía: sus segmentos continúan en Swap.
                // El worker de mediano plazo escribirá tras des-suspender.
                cambiar_estado(proc, SUSP_READY);
                log_info(logger, "## (%d) finalizó IO y pasa a SUSP. READY", pid);
                pthread_mutex_lock(&mutex_susp_ready);
                queue_push(cola_susp_ready, proc);
                pthread_mutex_unlock(&mutex_susp_ready);
                sem_post(&sem_largo_plazo);
            } else {
                t_stdin_pendiente* sp = extraer_stdin_pendiente(pid);
                int escritura_ok = escribir_stdin_en_km(sp) == 0;
                destruir_stdin_pendiente(sp);
                proc->esperando_stdin = 0;
                if (escritura_ok) {
                    cambiar_estado(proc, READY);
                    log_info(logger, "## (%d) finalizó IO y pasa a READY", pid);
                    encolar_en_ready(proc);
                } else {
                    log_warning(logger, "## (%d) - STDIN: error al escribir datos en KM", pid);
                    finalizar_por_error_io(proc, "Kernel Memory rechazó la escritura de STDIN");
                }
            }
            pthread_mutex_unlock(&mutex_transiciones_io);
        }

        free_mensaje(msg);
    }
}

static const char* op_code_to_string_kernel(uint32_t op_code) {
    switch (op_code) {
        case MSG_SYSCALL_SLEEP:    return "SLEEP";
        case MSG_INIT_PROC:        return "INIT_PROC";
        case MSG_SYSCALL_STDOUT:   return "STDOUT";
        case MSG_SYSCALL_STDIN:    return "STDIN";
        case MSG_SYSCALL_EXIT:     return "EXIT";
        case MSG_MEM_ALLOC:        return "MEM_ALLOC";
        case MSG_MEM_FREE:         return "MEM_FREE";
        case MSG_MUTEX_CREATE:     return "MUTEX_CREATE";
        case MSG_MUTEX_LOCK:       return "MUTEX_LOCK";
        case MSG_MUTEX_UNLOCK:     return "MUTEX_UNLOCK";
        case MSG_DEVOLVER_PROCESO: return "DEVOLVER_PROCESO";
        default:                   return "DESCONOCIDO";
    }
}
