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
void* handshake_kernel_memory(void* arg);

int main(int argc, char* argv[]) {
    if (argc < 4)
    {
        fprintf(stderr, "Cantidad de argumentos invalida.\n");
        fprintf(stderr, "Uso: %s [IP_KM] [PUERTO_KM] [PUERTO_ESCUCHA]\n", argv[0]);
        return EXIT_FAILURE;
    }

    char* ip_km          = argv[1];
    char* puerto_km      = argv[2];
    char* puerto_escucha = argv[3];

    int puerto_km_int      = atoi(puerto_km);
    int puerto_escucha_int = atoi(puerto_escucha);

    logger = log_create("kernel_scheduler.log", "KernelScheduler", true, LOG_LEVEL_INFO);
    if (logger == NULL) {
        fprintf(stderr, "Error al crear el logger\n");
        return EXIT_FAILURE;
    }

    // conectar a Kernel Memory
    int fd_km = conectar_a_servidor(ip_km, puerto_km_int);
    if (fd_km == -1) {
        log_error(logger, "No se pudo conectar a Kernel Memory");
        return EXIT_FAILURE;
    }

    // crear thread para handshake con KM (NO bloquea main)
    pthread_t thread_km;

    int* fd_km_mem = malloc(sizeof(int));
    *fd_km_mem = fd_km;

    pthread_create(&thread_km, NULL, handshake_kernel_memory, fd_km_mem);
    pthread_detach(thread_km);

    // crear servidor para CPUs / IO
    int fd_servidor = crear_servidor(puerto_escucha_int);
    
    if (fd_servidor < 0) {
        log_error(logger, "Error al crear servidor");
        return EXIT_FAILURE;
    }

    // aceptar conexiones concurrentes
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

// ---------------- THREAD HANDSHAKE KM ----------------

void* handshake_kernel_memory(void* arg) {
    int fd_km = *((int*)arg);
    free(arg);

    // mandar handshake
    enviar_mensaje(fd_km, MSG_KS_IDENTIFICACION, NULL, 0);

    // esperar respuesta
    t_mensaje* msg = recibir_mensaje(fd_km);

    if (msg == NULL) {
        log_error(logger, "No se recibio respuesta de Kernel Memory");
        return NULL;
    }

    if (msg->op_code != MSG_OK) {
        log_error(logger, "Kernel Memory rechazo la conexion");
        free_mensaje(msg);
        return NULL;
    }

    free_mensaje(msg);

    log_info(logger, "## Conectado a Kernel Memory");

    return NULL;
}

void* atender_cliente(void* arg){
    int fd_cliente = *((int*)arg);
    free(arg);
    
    // recibir handshake
    t_mensaje* msg = recibir_mensaje(fd_cliente);
    if(msg == NULL) return NULL;
    
    // log según quien se conecta
    if(msg->op_code == MSG_CPU_IDENTIFICACION){
        char* id_cpu = deserializar_string(msg->payload);
        log_info(logger, "## CPU %s Conectada", id_cpu);
        free(id_cpu);
    } 
    else if(msg->op_code == MSG_IO_IDENTIFICACION){
        char* tipo = deserializar_string(msg->payload);
        log_info(logger, "## IO %s Conectada", tipo);
        free(tipo); 
    }
    
    free_mensaje(msg);
    return NULL;
}