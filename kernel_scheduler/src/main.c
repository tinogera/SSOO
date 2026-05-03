#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <commons/log.h>
#include <commons/collections/queue.h>
#include <commons/config.h>
#include <utils/sockets.h>
#include <utils/protocolo.h>
#include <proceso.h>

t_log* logger;

int fd_km; // conexión persistente con Kernel Memory

t_queue *cola_new;
t_queue *cola_ready;
t_queue *cola_exec;
t_queue *cola_block;
t_queue *cola_susp_block;
t_queue *cola_susp_ready;
t_queue *cola_exit;

pthread_mutex_t mutex_new;
pthread_mutex_t mutex_ready;
pthread_mutex_t mutex_exec;
pthread_mutex_t mutex_block;
pthread_mutex_t mutex_susp_block;
pthread_mutex_t mutex_susp_ready;
pthread_mutex_t mutex_exit;

static uint32_t         next_pid  = 0;
static pthread_mutex_t  mutex_pid = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t  mutex_km  = PTHREAD_MUTEX_INITIALIZER;

void*      atender_cliente(void* arg);
t_proceso* crear_proceso(char* path, int prioridad);

static const char* estado_str(t_estado e) {
    switch (e) {
        case NEW:        return "NEW";
        case READY:      return "READY";
        case EXEC:       return "EXEC";
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


int main(int argc, char* argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Uso: %s [Archivo Config] [Path Proceso Inicial]\n", argv[0]);
        return EXIT_FAILURE;
    }

    char* config_path          = argv[1];
    char* path_proceso_inicial = argv[2];

    t_config* config = config_create(config_path);
    if (config == NULL) {
        fprintf(stderr, "No se pudo leer el archivo de configuración: %s\n", config_path);
        return EXIT_FAILURE;
    }

    char* log_level_str = config_get_string_value(config, "LOG_LEVEL");
    logger = log_create("kernel_scheduler.log", "KernelScheduler", true, log_level_from_string(log_level_str));
    if (logger == NULL) {
        fprintf(stderr, "Error al crear el logger\n");
        config_destroy(config);
        return EXIT_FAILURE;
    }

    char* ip_km     = config_get_string_value(config, "KERNEL_MEMORY_IP");
    int   puerto_km = config_get_int_value(config,    "KERNEL_MEMORY_PORT");
    int   puerto_ks = config_get_int_value(config,    "KERNEL_SCHEDULER_PORT");

    // Conectar a KM y hacer handshake de forma sincrónica.
    // Necesitamos confirmación antes de crear PID 0.
    fd_km = conectar_a_servidor(ip_km, puerto_km);
    if (fd_km == -1) {
        log_error(logger, "No se pudo conectar a Kernel Memory en %s:%d", ip_km, puerto_km);
        config_destroy(config);
        log_destroy(logger);
        return EXIT_FAILURE;
    }

    enviar_mensaje(fd_km, MSG_KS_IDENTIFICACION, NULL, 0);
    t_mensaje* resp_km = recibir_mensaje(fd_km);
    if (resp_km == NULL || resp_km->op_code != MSG_OK) {
        log_error(logger, "Kernel Memory rechazo la conexion");
        if (resp_km) free_mensaje(resp_km);
        config_destroy(config);
        log_destroy(logger);
        return EXIT_FAILURE;
    }
    free_mensaje(resp_km);
    log_info(logger, "## Conectado a Kernel Memory");

    // Inicializar colas y mutexes
    cola_new        = queue_create();
    cola_ready      = queue_create();
    cola_exec       = queue_create();
    cola_block      = queue_create();
    cola_susp_block = queue_create();
    cola_susp_ready = queue_create();
    cola_exit       = queue_create();

    pthread_mutex_init(&mutex_new,        NULL);
    pthread_mutex_init(&mutex_ready,      NULL);
    pthread_mutex_init(&mutex_exec,       NULL);
    pthread_mutex_init(&mutex_block,      NULL);
    pthread_mutex_init(&mutex_susp_block, NULL);
    pthread_mutex_init(&mutex_susp_ready, NULL);
    pthread_mutex_init(&mutex_exit,       NULL);

    // Crear servidor antes de PID 0 para que KM pueda responder mientras
    int fd_servidor = crear_servidor(puerto_ks);
    if (fd_servidor < 0) {
        log_error(logger, "Error al crear servidor en puerto %d", puerto_ks);
        config_destroy(config);
        log_destroy(logger);
        return EXIT_FAILURE;
    }

    // PID 0: proceso inicial con prioridad máxima (0)
    if (crear_proceso(path_proceso_inicial, 0) == NULL) {
        log_error(logger, "No se pudo crear el proceso inicial");
        config_destroy(config);
        log_destroy(logger);
        return EXIT_FAILURE;
    }

    // Loop principal: aceptar conexiones de CPUs e IOs
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


t_proceso* crear_proceso(char* path, int prioridad) {
    // Asignar PID de forma atómica
    pthread_mutex_lock(&mutex_pid);
    uint32_t pid = next_pid++;
    pthread_mutex_unlock(&mutex_pid);

    // Armar struct
    t_proceso* proc = malloc(sizeof(t_proceso));
    proc->PID                    = (int)pid;
    proc->estado                 = NEW;
    proc->controladorDeProgramas = 0;
    proc->prioridad              = prioridad;

    log_info(logger, "## (%u) Se crea el proceso - Estado: NEW", pid);

    uint32_t path_len     = strlen(path) + 1;
    uint32_t payload_size = sizeof(uint32_t) + path_len;
    void*    payload      = malloc(payload_size);
    uint32_t pid_n        = htonl(pid);
    memcpy(payload,                        &pid_n, sizeof(uint32_t));
    memcpy((char*)payload + sizeof(uint32_t), path, path_len);

    pthread_mutex_lock(&mutex_km);
    enviar_mensaje(fd_km, MSG_CREAR_PROCESO, payload, payload_size);
    free(payload);
    t_mensaje* resp = recibir_mensaje(fd_km);
    pthread_mutex_unlock(&mutex_km);

    if (resp == NULL || resp->op_code != MSG_OK) {
        log_error(logger, "KM rechazo la creacion del proceso %u", pid);
        if (resp) free_mensaje(resp);
        free(proc);
        return NULL;
    }
    free_mensaje(resp);

    // NEW → READY (largo plazo: sin restricciones)
    cambiar_estado(proc, READY);

    pthread_mutex_lock(&mutex_ready);
    queue_push(cola_ready, proc);
    pthread_mutex_unlock(&mutex_ready);

    return proc;
}

void* atender_cliente(void* arg) {
    int fd_cliente = *((int*)arg);
    free(arg);

    t_mensaje* msg = recibir_mensaje(fd_cliente);
    if (msg == NULL) return NULL;

    if (msg->op_code == MSG_CPU_IDENTIFICACION) {
        char* id_cpu = deserializar_string(msg->payload);
        log_info(logger, "## CPU %s Conectada", id_cpu);
        free(id_cpu);
    } else if (msg->op_code == MSG_IO_IDENTIFICACION) {
        char* tipo = deserializar_string(msg->payload);
        log_info(logger, "## IO %s Conectada", tipo);
        free(tipo);
    }

    free_mensaje(msg);
    return NULL;
}
