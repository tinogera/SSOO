#include <stdio.h>
#include <stdlib.h>

#include <commons/log.h>
#include <commons/config.h>

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

    // TODO: conectar al Kernel Scheduler

    config_destroy(config);
    log_destroy(logger);
    return EXIT_SUCCESS;
}
