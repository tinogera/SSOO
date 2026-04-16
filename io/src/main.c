#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <commons/log.h>
#include <commons/config.h>

#include <utils/sockets.h>
#include <utils/protocolo.h>

#include "io_utils.h"

t_log* logger;

int main(int argc, char* argv[]) {
    // -------------------------------------------------------------------
    // 1. Validar argumentos
    // -------------------------------------------------------------------
    if (argc < 3) {
        fprintf(stderr, "Uso: %s [Archivo Config] [Tipo]\n", argv[0]);
        fprintf(stderr, "Tipo debe ser: STDIN, STDOUT o SLEEP\n");
        return EXIT_FAILURE;
    }

    char* config_path = argv[1];
    char* tipo        = argv[2];

    if (!es_tipo_valido(tipo)) {
        fprintf(stderr, "Tipo inválido: %s. Debe ser STDIN, STDOUT o SLEEP\n", tipo);
        return EXIT_FAILURE;
    }

    // -------------------------------------------------------------------
    // 2. Inicializar logger
    // -------------------------------------------------------------------
    char log_file[64];
    snprintf(log_file, sizeof(log_file), "io_%s.log", tipo);

    logger = log_create(log_file, "IO", true, LOG_LEVEL_INFO);
    if (logger == NULL) {
        fprintf(stderr, "Error al crear el logger\n");
        return EXIT_FAILURE;
    }

    log_info(logger, "Iniciando IO tipo %s", tipo);

    // -------------------------------------------------------------------
    // 3. Leer configuración
    // -------------------------------------------------------------------
    t_config* config = config_create(config_path);
    if (config == NULL) {
        log_error(logger, "No se pudo leer el archivo de configuración: %s", config_path);
        log_destroy(logger);
        return EXIT_FAILURE;
    }

    log_info(logger, "Configuración cargada desde %s", config_path);

    // -------------------------------------------------------------------
    // 4. Conectarse al Kernel Scheduler
    // -------------------------------------------------------------------
    char* ks_ip = config_get_string_value(config, "KERNEL_SCHEDULER_IP");
    if (ks_ip == NULL) {
        log_error(logger, "Falta KERNEL_SCHEDULER_IP en el archivo de configuración");
        config_destroy(config);
        log_destroy(logger);
        return EXIT_FAILURE;
    }

    int ks_port = config_get_int_value(config, "KERNEL_SCHEDULER_PORT");

    int fd_scheduler = conectar_a_servidor(ks_ip, ks_port);
    if (fd_scheduler < 0) {
        log_error(logger, "No se pudo conectar al Kernel Scheduler en %s:%d", ks_ip, ks_port);
        config_destroy(config);
        log_destroy(logger);
        return EXIT_FAILURE;
    }

    // -------------------------------------------------------------------
    // 5. Identificarse con el tipo de IO
    // -------------------------------------------------------------------
    uint32_t payload_size;
    void* payload = serializar_string(tipo, &payload_size);
    enviar_mensaje(fd_scheduler, MSG_IO_IDENTIFICACION, payload, payload_size);
    free(payload);

    // Log obligatorio de la consigna
    log_info(logger, "## Conectado a Kernel Scheduler");

    // -------------------------------------------------------------------
    // 6. Loop principal — Check 2
    // -------------------------------------------------------------------
    // TODO Check 2: implementar bucle de recepción de pedidos IO

    close(fd_scheduler);
    config_destroy(config);
    log_destroy(logger);
    return EXIT_SUCCESS;
}
