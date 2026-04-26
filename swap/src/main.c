#include <stdio.h>
#include <stdlib.h>
#include <commons/log.h>
#include <commons/config.h>
#include <utils/sockets.h>
#include <utils/protocolo.h>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Uso: %s [CONFIG]\n", argv[0]);
        return EXIT_FAILURE;
    }

    t_config* config = config_create(argv[1]);
    if (config == NULL) {
        fprintf(stderr, "No se pudo leer el config: %s\n", argv[1]);
        return EXIT_FAILURE;
    }

    t_log* logger = log_create("swap.log", "Swap", true, LOG_LEVEL_INFO);
    if (logger == NULL) {
        fprintf(stderr, "Error al crear el logger\n");
        return EXIT_FAILURE;
    }

    char* ip_km     = config_get_string_value(config, "KERNEL_MEMORY_IP");
    int   puerto_km = config_get_int_value(config, "KERNEL_MEMORY_PORT");

    int fd = conectar_a_servidor(ip_km, puerto_km);
    if (fd < 0) {
        log_error(logger, "No se pudo conectar a Kernel Memory en %s:%d", ip_km, puerto_km);
        config_destroy(config);
        log_destroy(logger);
        return EXIT_FAILURE;
    }

    enviar_mensaje(fd, MSG_SWAP_IDENTIFICACION, NULL, 0);

    t_mensaje* respuesta = recibir_mensaje(fd);
    if (respuesta == NULL || respuesta->op_code != MSG_OK) {
        log_error(logger, "Kernel Memory rechazo la conexion");
        if (respuesta) free_mensaje(respuesta);
        config_destroy(config);
        log_destroy(logger);
        return EXIT_FAILURE;
    }
    free_mensaje(respuesta);

    log_info(logger, "## Conectado a Kernel Memory");

    // mantener vivo hasta Check 2
    while (1);

    config_destroy(config);
    log_destroy(logger);
    return EXIT_SUCCESS;
}
