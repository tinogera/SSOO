#include <stdio.h>
#include <stdlib.h>
#include <commons/log.h>
#include <commons/config.h>

#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <string.h>

int crear_conexion(char* ip, char* puerto) {
    struct addrinfo hints, *servinfo;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    getaddrinfo(ip, puerto, &hints, &servinfo);

    int socket_cliente = socket(servinfo->ai_family,
                                servinfo->ai_socktype,
                                servinfo->ai_protocol);

    connect(socket_cliente, servinfo->ai_addr, servinfo->ai_addrlen);

    freeaddrinfo(servinfo);

    return socket_cliente;
}

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

    // LEER CONFIG
    char* ip_kernel = config_get_string_value(config, "IP_KERNEL");
    char* puerto_kernel = config_get_string_value(config, "PUERTO_KERNEL");

    char* ip_memory = config_get_string_value(config, "IP_MEMORY");
    char* puerto_memory = config_get_string_value(config, "PUERTO_MEMORY");

    //  CONECTAR A KERNEL SCHEDULER
    int socket_kernel = crear_conexion(ip_kernel, puerto_kernel);
    log_info(logger, "Conectado a Kernel Scheduler");

    //  CONECTAR A KERNEL MEMORY
    int socket_memory = crear_conexion(ip_memory, puerto_memory);
    log_info(logger, "Conectado a Kernel Memory");

    // mantener vivo
    while(1);

    close(socket_kernel);
    close(socket_memory);

    log_destroy(logger);
    config_destroy(config);

    return 0;
}
