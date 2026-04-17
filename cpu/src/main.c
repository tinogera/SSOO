#include <stdio.h>
#include <stdlib.h>
#include <commons/log.h>
#include <commons/config.h>

#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <string.h>

int crear_conexion(char* ip, char* puerto) {
    struct addrinfo hints, *servinfo, *p;
    int socket_cliente;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(ip, puerto, &hints, &servinfo) != 0) {
        return -1;
    }

    for (p = servinfo; p != NULL; p = p->ai_next) {
        socket_cliente = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (socket_cliente == -1) continue;

        if (connect(socket_cliente, p->ai_addr, p->ai_addrlen) == 0) {
            freeaddrinfo(servinfo);
            return socket_cliente;
        }

        close(socket_cliente);
    }

    freeaddrinfo(servinfo);
    return -1;
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
if (socket_kernel == -1) {
    log_error(logger, "No se pudo conectar a Kernel Scheduler");
} else {
    log_info(logger, "Conectado a Kernel Scheduler");
}

    //  CONECTAR A KERNEL MEMORY
   int socket_memory = crear_conexion(ip_memory, puerto_memory);
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
