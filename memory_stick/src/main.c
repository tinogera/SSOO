#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
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
                log_warning(logger, "Memory Stick conectado sin payload de tamaño (fd=%d)", fd);
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
                log_info(logger, "## Swap Conectado - FD: %d - Tamaño: %u bytes - Bloque: %u bytes - Bloques totales: %u",
                         fd, swap_size, block_size, cant_bloques);
            } else {
                log_warning(logger, "Swap conectado sin payload de capacidad (fd=%d)", fd);
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
    // Loop de atención — Check 2 en adelante
    // ------------------------------------------------------------------
    t_mensaje* pedido;
    while ((pedido = recibir_mensaje(fd)) != NULL) {
        // TODO Check 2: despachar según pedido->op_code
        // (fetch instrucción, guardar/restaurar contexto, etc.)
        log_warning(logger, "Pedido recibido pero no implementado aún: op_code=%d", pedido->op_code);
        enviar_mensaje(fd, MSG_ERROR, NULL, 0);
        free_mensaje(pedido);
    }

    log_info(logger, "Cliente desconectado (fd=%d)", fd);
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
        config_destroy(config);
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
