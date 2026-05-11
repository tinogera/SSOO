#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <pthread.h>
#include <unistd.h>
#include <commons/log.h>
#include <commons/config.h>
#include <utils/sockets.h>
#include <utils/protocolo.h>

t_log* logger;

typedef struct {
    int fd;
} t_cliente_args;

typedef struct {
    uint32_t pid;
    t_contexto_ejecucion_cpu contexto;
    bool ocupado;
} t_contexto_por_pid;

#define MAX_CONTEXTO_PROCESOS 128

static t_contexto_por_pid contextos[MAX_CONTEXTO_PROCESOS];
static pthread_mutex_t mutex_contextos = PTHREAD_MUTEX_INITIALIZER;

static void atender_cpu(int fd);
static void atender_fetch_instruccion(int fd, t_mensaje* msg);
static void atender_obtener_contexto(int fd, t_mensaje* msg);
static void atender_guardar_contexto(int fd, t_mensaje* msg);
static t_contexto_ejecucion_cpu* obtener_o_crear_contexto(uint32_t pid);
static const char* obtener_instruccion_mock(uint32_t pid, uint32_t pc);

static void* atender_cliente(void* arg) {
    t_cliente_args* args = (t_cliente_args*)arg;
    int fd = args->fd;
    free(args);

    t_mensaje* msg = recibir_mensaje(fd);
    if (msg == NULL) {
        log_warning(logger, "Cliente desconectado antes de identificarse (fd=%d)", fd);
        return NULL;
    }

    switch (msg->op_code) {

        case MSG_KS_IDENTIFICACION:
            log_info(logger, "## Kernel Scheduler Conectado - FD del socket: %d", fd);
            enviar_mensaje(fd, MSG_OK, NULL, 0);
            break;

        case MSG_MEMORY_STICK_IDENTIFICACION:
            log_info(logger, "## Memory Stick Conectado - FD del socket: %d", fd);
            enviar_mensaje(fd, MSG_OK, NULL, 0);
            break;

        case MSG_SWAP_IDENTIFICACION:
            log_info(logger, "## Swap Conectado - FD del socket: %d", fd);
            enviar_mensaje(fd, MSG_OK, NULL, 0);
            break;

        case MSG_CPU_A_KERNEL_MEMORY:
            log_info(logger, "## CPU Conectada - FD del socket: %d", fd);
            enviar_mensaje(fd, MSG_OK, NULL, 0);
            free_mensaje(msg);
            atender_cpu(fd);
            return NULL;

        default:
            log_warning(logger, "op_code desconocido: %d (fd=%d)", msg->op_code, fd);
            enviar_mensaje(fd, MSG_ERROR, NULL, 0);
            break;
    }

    free_mensaje(msg);
    // Check 1: cerramos después de identificar. Check 2: acá irá el loop de atención.
    return NULL;
}

static void atender_cpu(int fd) {
    while (1) {
        t_mensaje* msg = recibir_mensaje(fd);
        if (msg == NULL) {
            log_info(logger, "CPU desconectada de Kernel Memory - FD del socket: %d", fd);
            break;
        }

        switch (msg->op_code) {
            case MSG_FETCH_INSTRUCCION:
                atender_fetch_instruccion(fd, msg);
                break;
            case MSG_OBTENER_CONTEXTO:
                atender_obtener_contexto(fd, msg);
                break;
            case MSG_GUARDAR_CONTEXTO:
                atender_guardar_contexto(fd, msg);
                break;
            default:
                log_warning(logger, "Mensaje inesperado de CPU en Kernel Memory: %d", msg->op_code);
                enviar_mensaje(fd, MSG_ERROR, NULL, 0);
                break;
        }

        free_mensaje(msg);
    }

    close(fd);
}

static void atender_fetch_instruccion(int fd, t_mensaje* msg) {
    if (msg->payload_size != sizeof(t_payload_fetch_instruccion)) {
        log_error(logger, "Payload invalido para fetch de instruccion");
        enviar_mensaje(fd, MSG_ERROR, NULL, 0);
        return;
    }

    t_payload_fetch_instruccion* pedido = (t_payload_fetch_instruccion*) msg->payload;
    const char* instruccion = obtener_instruccion_mock(pedido->pid, pedido->pc);

    log_info(
        logger,
        "## PID: %u - Obtener instruccion: %u - Instruccion: %s",
        pedido->pid,
        pedido->pc,
        instruccion
    );

    uint32_t payload_size;
    void* payload = serializar_string((char*) instruccion, &payload_size);
    enviar_mensaje(fd, MSG_RESPUESTA_INSTRUCCION, payload, payload_size);
    free(payload);
}

static void atender_obtener_contexto(int fd, t_mensaje* msg) {
    if (msg->payload_size != sizeof(t_payload_obtener_contexto)) {
        log_error(logger, "Payload invalido para obtener contexto");
        enviar_mensaje(fd, MSG_ERROR, NULL, 0);
        return;
    }

    t_payload_obtener_contexto* pedido = (t_payload_obtener_contexto*) msg->payload;

    pthread_mutex_lock(&mutex_contextos);
    t_contexto_ejecucion_cpu* contexto = obtener_o_crear_contexto(pedido->pid);
    if (contexto == NULL) {
        pthread_mutex_unlock(&mutex_contextos);
        log_error(logger, "No hay espacio para crear contexto del PID %u", pedido->pid);
        enviar_mensaje(fd, MSG_ERROR, NULL, 0);
        return;
    }

    t_payload_contexto_cpu respuesta = {
        .pid = pedido->pid,
        .contexto = *contexto
    };
    pthread_mutex_unlock(&mutex_contextos);

    log_info(logger, "## PID: %u - Contexto enviado - PC: %u", respuesta.pid, respuesta.contexto.pc);
    enviar_mensaje(fd, MSG_RESPUESTA_CONTEXTO, &respuesta, sizeof(respuesta));
}

static void atender_guardar_contexto(int fd, t_mensaje* msg) {
    if (msg->payload_size != sizeof(t_payload_contexto_cpu)) {
        log_error(logger, "Payload invalido para guardar contexto");
        enviar_mensaje(fd, MSG_ERROR, NULL, 0);
        return;
    }

    t_payload_contexto_cpu* payload = (t_payload_contexto_cpu*) msg->payload;

    pthread_mutex_lock(&mutex_contextos);
    t_contexto_ejecucion_cpu* contexto = obtener_o_crear_contexto(payload->pid);
    if (contexto == NULL) {
        pthread_mutex_unlock(&mutex_contextos);
        log_error(logger, "No hay espacio para guardar contexto del PID %u", payload->pid);
        enviar_mensaje(fd, MSG_ERROR, NULL, 0);
        return;
    }

    *contexto = payload->contexto;
    pthread_mutex_unlock(&mutex_contextos);

    log_info(logger, "## PID: %u - Contexto guardado - PC: %u", payload->pid, payload->contexto.pc);
    enviar_mensaje(fd, MSG_OK, NULL, 0);
}

static t_contexto_ejecucion_cpu* obtener_o_crear_contexto(uint32_t pid) {
    int libre = -1;

    for (int i = 0; i < MAX_CONTEXTO_PROCESOS; i++) {
        if (contextos[i].ocupado && contextos[i].pid == pid) {
            return &contextos[i].contexto;
        }

        if (!contextos[i].ocupado && libre == -1) {
            libre = i;
        }
    }

    if (libre == -1) {
        return NULL;
    }

    contextos[libre].pid = pid;
    memset(&contextos[libre].contexto, 0, sizeof(contextos[libre].contexto));
    contextos[libre].ocupado = true;
    log_info(logger, "## PID: %u - Proceso Creado", pid);

    return &contextos[libre].contexto;
}

static const char* obtener_instruccion_mock(uint32_t pid, uint32_t pc) {
    (void) pid;

    static const char* instrucciones[] = {
        "SET AX 2",
        "SET BX 3",
        "SUM AX BX",
        "EXIT"
    };

    uint32_t cantidad_instrucciones = sizeof(instrucciones) / sizeof(instrucciones[0]);
    if (pc >= cantidad_instrucciones) {
        return "EXIT";
    }

    return instrucciones[pc];
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Uso: %s [Archivo Config]\n", argv[0]);
        return EXIT_FAILURE;
    }

    t_config* config = config_create(argv[1]);
    if (config == NULL) {
        fprintf(stderr, "No se pudo leer el config: %s\n", argv[1]);
        return EXIT_FAILURE;
    }

    logger = log_create("kernel_memory.log", "KernelMemory", true, LOG_LEVEL_INFO);
    if (logger == NULL) {
        fprintf(stderr, "Error al crear el logger\n");
        return EXIT_FAILURE;
    }

    int puerto = config_get_int_value(config, "KERNEL_MEMORY_PORT");

    int servidor_fd = crear_servidor(puerto);
    if (servidor_fd < 0) {
        log_error(logger, "No se pudo levantar el servidor en puerto %d", puerto);
        config_destroy(config);
        log_destroy(logger);
        return EXIT_FAILURE;
    }

    log_info(logger, "Kernel Memory escuchando en puerto %d", puerto);

    // Loop: acepta conexiones indefinidamente, una por hilo
    while (1) {
        int cliente_fd = aceptar_conexion(servidor_fd);
        if (cliente_fd < 0) {
            log_warning(logger, "Error al aceptar cliente");
            continue;
        }

        t_cliente_args* args = malloc(sizeof(t_cliente_args));
        args->fd = cliente_fd;

        pthread_t hilo;
        pthread_create(&hilo, NULL, atender_cliente, args);
        pthread_detach(hilo);
    }

    config_destroy(config);
    log_destroy(logger);
    return EXIT_SUCCESS;
}
