#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <commons/log.h>
#include <commons/config.h>
#include <utils/sockets.h>
#include <utils/protocolo.h>


t_log* logger;

void* atender_cliente(void* arg);

int main(int argc, char* argv[]) {
    if (argc < 3)
    {
        fprintf(stderr, "Cantidad de argumentos invalida.\n");
        fprintf(stderr, "Uso: %s [Archivo Config] [Tamaño]\n", argv[0]);
        return EXIT_FAILURE;
    }

    char* ip_km          = argv[1];
    char* puerto_km      = argv[2];
    char* puerto_escucha = argv[3];

    logger = log_create("kernel_scheduler.log", "KernelScheduler", true, LOG_LEVEL_INFO);
    if (logger == NULL) {
        fprintf(stderr, "Error al crear el logger\n");
        return EXIT_FAILURE;
    }

    int fd_km = conectar_a_servidor(ip_km, puerto_km);
    if (fd_km == -1) {
        log_error(logger, "No se pudo conectar a Kernel Memory");
        return EXIT_FAILURE;
    }

    enviar_mensaje(fd_km, MSG_KS_IDENTIFICACION, NULL, 0);

    t_mensaje* msg = recibir_mensaje(fd_km);

    if(msg==NULL){
        log_error(logger, "No se recibio respuesta de Kernel Memory");
        return EXIT_FAILURE;
    }  // esperar OK
    if (msg->op_code != MSG_OK) {
        log_error(logger, "Kernel Memory rechazo la conexion");
        free(msg);
        return EXIT_FAILURE;
    }
    free(msg);

    log_info(logger, "## Conectado a Kernel Memory");

    int fd_servidor = crear_servidor(puerto_escucha);
    
    if (fd_servidor < 0) {
        log_error(logger, "Error al crear servidor");
        return EXIT_FAILURE;
    }

    while(1){
        int fd_cliente = aceptar_conexion(fd_servidor);
        pthread_t hilo;

        int* fd_cliente_mem = malloc(sizeof(int));
        *fd_cliente_mem = fd_cliente;

        pthread_create(&hilo, NULL, atender_cliente, fd_cliente_mem);

        pthread_detach(hilo);
    }

    return 0;
}

void* atender_cliente(void* arg){
    int fd_cliente = *((int*)arg);
    free(arg);
    
    // recibir handshake
    t_mensaje* msg = recibir_mensaje(fd_cliente);
    if(msg == NULL) return NULL;
    
    // según op_code loguear
    if(msg->op_code == MSG_CPU_IDENTIFICACION){
        // loguear CPU
    } else if(msg->op_code == MSG_IO_IDENTIFICACION){
        // loguear IO
    }
    
    free(msg);
    return NULL;
}