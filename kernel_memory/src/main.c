#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <commons/log.h>
#include <commons/config.h>
#include <utils/sockets.h>
#include <utils/protocolo.h>

t_log* logger;

typedef struct {
    int fd;
} t_cliente_args;

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
            break;

        default:
            log_warning(logger, "op_code desconocido: %d (fd=%d)", msg->op_code, fd);
            enviar_mensaje(fd, MSG_ERROR, NULL, 0);
            break;
    }

    free_mensaje(msg);
    // Check 1: cerramos después de identificar. Check 2: acá irá el loop de atención.
    return NULL;
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

    int servidor_fd = iniciar_servidor(puerto);
    if (servidor_fd < 0) {
        log_error(logger, "No se pudo levantar el servidor en puerto %d", puerto);
        config_destroy(config);
        log_destroy(logger);
        return EXIT_FAILURE;
    }

    log_info(logger, "Kernel Memory escuchando en puerto %d", puerto);

    // Loop: acepta conexiones indefinidamente, una por hilo
    while (1) {
        int cliente_fd = aceptar_cliente(servidor_fd);
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
