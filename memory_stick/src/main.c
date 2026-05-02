#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <commons/log.h>
#include <commons/config.h>
#include <utils/sockets.h>
#include <utils/protocolo.h>

t_log* logger;

int main(int argc, char* argv[]) {

    if (argc < 3) {
        fprintf(stderr, "Uso: %s [Archivo Config] [Tamaño]\n", argv[0]);
        return EXIT_FAILURE;
    }

    char*    config_path = argv[1];
    uint32_t tamanio     = (uint32_t)atoi(argv[2]);

    t_config* config = config_create(config_path);
    if (config == NULL) {
        fprintf(stderr, "No se pudo leer el archivo de configuración: %s\n", config_path);
        return EXIT_FAILURE;
    }

    int   puerto      = config_get_int_value(config,    "MEMORY_STICK_PORT");
    int   id          = config_get_int_value(config,    "MEMORY_STICK_ID");
    int   delay       = config_get_int_value(config,    "MEMORY_DELAY");
    int   kernel_port = config_get_int_value(config,    "KERNEL_MEMORY_PORT");
    char* kernel_ip   = config_get_string_value(config, "KERNEL_MEMORY_IP");

    char log_file[64];
    snprintf(log_file, sizeof(log_file), "memory_stick_%d.log", id);

    logger = log_create(log_file, "MemoryStick", true, LOG_LEVEL_INFO);
    if (logger == NULL) {
        fprintf(stderr, "Error al crear el logger\n");
        config_destroy(config);
        return EXIT_FAILURE;
    }

    if (kernel_ip == NULL) {
        log_error(logger, "Falta KERNEL_MEMORY_IP en el archivo de configuración");
        config_destroy(config);
        log_destroy(logger);
        return EXIT_FAILURE;
    }

    // Conectarse a Kernel Memory
    int fd_km = conectar_a_servidor(kernel_ip, kernel_port);
    if (fd_km < 0) {
        log_error(logger, "No se pudo conectar a Kernel Memory en %s:%d", kernel_ip, kernel_port);
        config_destroy(config);
        log_destroy(logger);
        return EXIT_FAILURE;
    }

    // Identificarse con tamaño en el payload
    uint8_t  payload[4];
    uint32_t tamanio_n = htonl(tamanio);
    memcpy(payload, &tamanio_n, 4);

    enviar_mensaje(fd_km, MSG_MEMORY_STICK_IDENTIFICACION, payload, sizeof(payload));

    t_mensaje* respuesta = recibir_mensaje(fd_km);
    if (respuesta == NULL || respuesta->op_code != MSG_OK) {
        log_error(logger, "Kernel Memory rechazó la conexión");
        if (respuesta) free_mensaje(respuesta);
        config_destroy(config);
        log_destroy(logger);
        return EXIT_FAILURE;
    }
    free_mensaje(respuesta);

    log_info(logger, "## Conectado a Kernel Memory");

    // Levantar servidor para CPUs
    int fd_servidor = crear_servidor(puerto);
    if (fd_servidor < 0) {
        log_error(logger, "No se pudo levantar servidor en puerto %d", puerto);
        config_destroy(config);
        log_destroy(logger);
        return EXIT_FAILURE;
    }

    log_info(logger, "Escuchando CPUs en puerto %d", puerto);

    while (1) {
        int fd_cpu = aceptar_conexion(fd_servidor);
        if (fd_cpu < 0) {
            log_warning(logger, "Error al aceptar conexión de CPU");
            continue;
        }

        t_mensaje* msg_cpu = recibir_mensaje(fd_cpu);
        if (msg_cpu == NULL) {
            log_warning(logger, "CPU desconectada antes de identificarse");
            continue;
        }

        if (msg_cpu->op_code == MSG_CPU_IDENTIFICACION && msg_cpu->payload_size >= 4) {
            uint32_t cpu_id_n;
            memcpy(&cpu_id_n, msg_cpu->payload, 4);
            uint32_t cpu_id = ntohl(cpu_id_n);
            log_info(logger, "## CPU %u Conectada", cpu_id);
            enviar_mensaje(fd_cpu, MSG_OK, NULL, 0);
        } else {
            log_warning(logger, "Identificación de CPU inválida");
            enviar_mensaje(fd_cpu, MSG_ERROR, NULL, 0);
        }

        free_mensaje(msg_cpu);
        // TODO Check 2: loop de lectura/escritura por CPU
    }

    (void)delay; // se usa en Check 2 para MEMORY_DELAY

    config_destroy(config);
    log_destroy(logger);
    return EXIT_SUCCESS;
}
