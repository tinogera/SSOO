#include <stdio.h>
#include <stdlib.h>
#include <commons/log.h>
#include <commons/config.h>
#include <unistd.h>
#include <utils/sockets.h>

int main(int argc, char* argv[]) {

    if (argc < 3) {
        printf("Uso: ./cpu [CONFIG] [ID]\n");
        return 1;
    }

    char* config_path = argv[1];
    char* cpu_id = argv[2];

    t_config* config = config_create(config_path);
    t_log* logger = log_create("cpu.log", cpu_id, 1, LOG_LEVEL_INFO);

    log_info(logger, "CPU %s iniciada", cpu_id);

    char* ip_kernel = config_get_string_value(config, "IP_KERNEL");
    int puerto_kernel = config_get_int_value(config, "PUERTO_KERNEL");

    char* ip_memory = config_get_string_value(config, "IP_MEMORY");
    int puerto_memory = config_get_int_value(config, "PUERTO_MEMORY");

    int socket_kernel = conectar_a_servidor(ip_kernel, puerto_kernel);
    if (socket_kernel == -1) {
        log_error(logger, "No se pudo conectar a Kernel Scheduler");
    } else {
        log_info(logger, "Conectado a Kernel Scheduler");
    }

    int socket_memory = conectar_a_servidor(ip_memory, puerto_memory);
    if (socket_memory == -1) {
        log_error(logger, "No se pudo conectar a Kernel Memory");
    } else {
        log_info(logger, "Conectado a Kernel Memory");
    }

    // mantener vivo
    while(1);

    close(socket_kernel);
    close(socket_memory);

    log_destroy(logger);
    config_destroy(config);

    return 0;
}
