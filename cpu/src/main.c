#include <stdio.h>
#include <stdlib.h>
#include <commons/log.h>
#include <commons/config.h>

int main(int argc, char* argv[]) {

    if (argc < 3) {
        printf("Uso: ./cpu [CONFIG] [ID]\n");
        return 1;
    }

    char* config_path = argv[1];
    char* cpu_id = argv[2];

    // crear config
    t_config* config = config_create(config_path);

    // crear logger
    t_log* logger = log_create("cpu.log", cpu_id, 1, LOG_LEVEL_INFO);

    log_info(logger, "CPU %s iniciada", cpu_id);

    // (después conectamos sockets acá)

    log_destroy(logger);
    config_destroy(config);

    return 0;
}
