#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <pthread.h>
#include <unistd.h>
#include <commons/log.h>
#include <commons/config.h>
#include <utils/sockets.h>
#include <utils/protocolo.h>
#include "ks_mutex.h"

t_log* logger;

void* atender_cliente(void* arg);
void* handshake_kernel_memory(void* arg);
void atender_cpu(int fd_cpu, char* id_cpu);
void manejar_mensaje_cpu(int fd_cpu, t_mensaje* msg);
void despachar_proceso_a_cpu(int fd_cpu, uint32_t pid);
const char* motivo_devolucion_to_string_kernel(uint32_t motivo);
const char* op_code_to_string_kernel(uint32_t op_code);

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

    mutexes_init();

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

    // --- identificación inicial ---
    t_mensaje* msg = recibir_mensaje(fd_cliente);
    if(msg == NULL) return NULL;

    int es_cpu = 0;

    if(msg->op_code == MSG_CPU_IDENTIFICACION){
        char* id_cpu = deserializar_string(msg->payload);
        log_info(logger, "## CPU %s Conectada", id_cpu);
        free_mensaje(msg);
        atender_cpu(fd_cliente, id_cpu);
        free(id_cpu);
        enviar_mensaje(fd_cliente, MSG_OK, NULL, 0);
        es_cpu = 1;
    }
    else if(msg->op_code == MSG_IO_IDENTIFICACION){
        char* tipo = deserializar_string(msg->payload);
        log_info(logger, "## IO %s Conectada", tipo);
        free(tipo);
        enviar_mensaje(fd_cliente, MSG_OK, NULL, 0);
    }
    else {
        log_warning(logger, "Identificación desconocida: op_code=%u", msg->op_code);
    }

    free_mensaje(msg);

    // --- loop de atención (solo CPUs por ahora) ---
    if (!es_cpu) return NULL;

    t_mensaje* pedido;
    while((pedido = recibir_mensaje(fd_cliente)) != NULL) {

        if (pedido->op_code == MSG_MUTEX_CREATE) {
            t_payload_mutex* p = pedido->payload;
            mutex_ks_create(p->nombre);
            enviar_mensaje(fd_cliente, MSG_OK, NULL, 0);
        }
        else if (pedido->op_code == MSG_MUTEX_LOCK) {
            t_payload_mutex* p = pedido->payload;
            uint32_t pid = p->pid;
            // mutex_ks_lock responde MSG_OK directamente si el mutex está libre,
            // o no responde hasta que mutex_ks_unlock desbloquee al waiter.
            mutex_ks_lock(pid, fd_cliente, p->nombre, logger);
        }
        else if (pedido->op_code == MSG_MUTEX_UNLOCK) {
            t_payload_mutex* p = pedido->payload;
            mutex_ks_unlock(p->pid, p->nombre, logger);
            enviar_mensaje(fd_cliente, MSG_OK, NULL, 0);
        }
        else {
            log_warning(logger, "op_code no manejado en KS: %u", pedido->op_code);
            enviar_mensaje(fd_cliente, MSG_ERROR, NULL, 0);
        }

        free_mensaje(pedido);
    }

    return NULL;
}

void atender_cpu(int fd_cpu, char* id_cpu) {
    uint32_t pid_inicial = 1;
    log_info(logger, "CPU %s queda disponible para ejecutar procesos", id_cpu);
    despachar_proceso_a_cpu(fd_cpu, pid_inicial);

    while (1) {
        t_mensaje* msg = recibir_mensaje(fd_cpu);
        if (msg == NULL) {
            log_info(logger, "CPU %s desconectada", id_cpu);
            break;
        }

        manejar_mensaje_cpu(fd_cpu, msg);

        bool fue_devolucion = msg->op_code == MSG_DEVOLVER_PROCESO;
        free_mensaje(msg);

        if (fue_devolucion) {
            log_info(logger, "CPU %s queda libre luego de devolver el proceso", id_cpu);
            break;
        }
    }

    close(fd_cpu);
}

void despachar_proceso_a_cpu(int fd_cpu, uint32_t pid) {
    t_payload_despachar_proceso payload = {
        .pid = pid
    };

    log_info(logger, "## PID: %u - Despachando proceso a CPU", pid);
    enviar_mensaje(fd_cpu, MSG_DESPACHAR_PROCESO, &payload, sizeof(payload));
}

void manejar_mensaje_cpu(int fd_cpu, t_mensaje* msg) {
    switch (msg->op_code) {
        case MSG_DEVOLVER_PROCESO: {
            if (msg->payload_size < sizeof(t_payload_devolver_proceso)) {
                log_error(logger, "CPU envio devolucion con payload invalido");
                enviar_mensaje(fd_cpu, MSG_ERROR, NULL, 0);
                return;
            }

            t_payload_devolver_proceso* payload = (t_payload_devolver_proceso*) msg->payload;
            log_info(
                logger,
                "## PID: %u - Proceso devuelto por CPU - Motivo: %s - PC: %u",
                payload->pid,
                motivo_devolucion_to_string_kernel(payload->motivo),
                payload->pc
            );
            return;
        }
        case MSG_INIT_PROC: {
            if (msg->payload_size < sizeof(t_payload_syscall_init_proc) + 1) {
                log_error(logger, "CPU envio INIT_PROC con payload invalido");
                enviar_mensaje(fd_cpu, MSG_ERROR, NULL, 0);
                return;
            }

            t_payload_syscall_init_proc* payload = (t_payload_syscall_init_proc*) msg->payload;
            log_info(
                logger,
                "## PID: %u - Syscall recibida: INIT_PROC - Archivo: %s - Prioridad: %u",
                payload->pid,
                payload->archivo,
                payload->prioridad
            );
            enviar_mensaje(fd_cpu, MSG_OK, NULL, 0);
            return;
        }
        case MSG_SYSCALL_SLEEP: {
            if (msg->payload_size < sizeof(t_payload_syscall_sleep)) {
                log_error(logger, "CPU envio SLEEP con payload invalido");
                enviar_mensaje(fd_cpu, MSG_ERROR, NULL, 0);
                return;
            }

            t_payload_syscall_sleep* payload = (t_payload_syscall_sleep*) msg->payload;
            log_info(logger, "## PID: %u - Syscall recibida: SLEEP - Tiempo: %u", payload->pid, payload->tiempo_ms);
            enviar_mensaje(fd_cpu, MSG_OK, NULL, 0);
            return;
        }
        case MSG_SYSCALL_STDOUT:
        case MSG_SYSCALL_STDIN: {
            if (msg->payload_size < sizeof(t_payload_syscall_io_memoria)) {
                log_error(logger, "CPU envio syscall IO con payload invalido");
                enviar_mensaje(fd_cpu, MSG_ERROR, NULL, 0);
                return;
            }

            t_payload_syscall_io_memoria* payload = (t_payload_syscall_io_memoria*) msg->payload;
            log_info(
                logger,
                "## PID: %u - Syscall recibida: %s - Direccion Logica: %u - Tamanio: %u",
                payload->pid,
                op_code_to_string_kernel(msg->op_code),
                payload->direccion_logica,
                payload->tamanio
            );
            enviar_mensaje(fd_cpu, MSG_OK, NULL, 0);
            return;
        }
        case MSG_SYSCALL_EXIT: {
            if (msg->payload_size < sizeof(t_payload_syscall_exit)) {
                log_error(logger, "CPU envio EXIT con payload invalido");
                enviar_mensaje(fd_cpu, MSG_ERROR, NULL, 0);
                return;
            }

            t_payload_syscall_exit* payload = (t_payload_syscall_exit*) msg->payload;
            log_info(logger, "## PID: %u - Syscall recibida: EXIT", payload->pid);
            enviar_mensaje(fd_cpu, MSG_OK, NULL, 0);
            return;
        }
        case MSG_MEM_ALLOC: {
            if (msg->payload_size < sizeof(t_payload_syscall_mem_alloc)) {
                log_error(logger, "CPU envio MEM_ALLOC con payload invalido");
                enviar_mensaje(fd_cpu, MSG_ERROR, NULL, 0);
                return;
            }

            t_payload_syscall_mem_alloc* payload = (t_payload_syscall_mem_alloc*) msg->payload;
            log_info(
                logger,
                "## PID: %u - Syscall recibida: MEM_ALLOC - Segmento: %u - Tamanio: %u",
                payload->pid,
                payload->id_segmento,
                payload->tamanio
            );
            enviar_mensaje(fd_cpu, MSG_OK, NULL, 0);
            return;
        }
        case MSG_MEM_FREE: {
            if (msg->payload_size < sizeof(t_payload_syscall_mem_free)) {
                log_error(logger, "CPU envio MEM_FREE con payload invalido");
                enviar_mensaje(fd_cpu, MSG_ERROR, NULL, 0);
                return;
            }

            t_payload_syscall_mem_free* payload = (t_payload_syscall_mem_free*) msg->payload;
            log_info(
                logger,
                "## PID: %u - Syscall recibida: MEM_FREE - Segmento: %u",
                payload->pid,
                payload->id_segmento
            );
            enviar_mensaje(fd_cpu, MSG_OK, NULL, 0);
            return;
        }
        case MSG_MUTEX_CREATE:
        case MSG_MUTEX_LOCK:
        case MSG_MUTEX_UNLOCK: {
            if (msg->payload_size < sizeof(uint32_t) + 1) {
                log_error(logger, "CPU envio syscall Mutex con payload invalido");
                enviar_mensaje(fd_cpu, MSG_ERROR, NULL, 0);
                return;
            }

            uint32_t pid;
            memcpy(&pid, msg->payload, sizeof(uint32_t));
            char* nombre = (char*) msg->payload + sizeof(uint32_t);
            log_info(logger, "## PID: %u - Syscall recibida: %s - %s", pid, op_code_to_string_kernel(msg->op_code), nombre);
            enviar_mensaje(fd_cpu, MSG_OK, NULL, 0);
            return;
        }
        default:
            log_error(logger, "Mensaje inesperado desde CPU: %s", op_code_to_string_kernel(msg->op_code));
            enviar_mensaje(fd_cpu, MSG_ERROR, NULL, 0);
            return;
    }
}

const char* motivo_devolucion_to_string_kernel(uint32_t motivo) {
    switch (motivo) {
        case MOTIVO_DEVOLUCION_SYSCALL:
            return "SYSCALL";
        case MOTIVO_DEVOLUCION_EXIT:
            return "EXIT";
        case MOTIVO_DEVOLUCION_ERROR:
            return "ERROR";
        case MOTIVO_DEVOLUCION_INTERRUPCION:
            return "INTERRUPCION";
        default:
            return "DESCONOCIDO";
    }
}

const char* op_code_to_string_kernel(uint32_t op_code) {
    switch (op_code) {
        case MSG_SYSCALL_SLEEP:
            return "SLEEP";
        case MSG_INIT_PROC:
            return "INIT_PROC";
        case MSG_SYSCALL_STDOUT:
            return "STDOUT";
        case MSG_SYSCALL_STDIN:
            return "STDIN";
        case MSG_SYSCALL_EXIT:
            return "EXIT";
        case MSG_MEM_ALLOC:
            return "MEM_ALLOC";
        case MSG_MEM_FREE:
            return "MEM_FREE";
        case MSG_MUTEX_CREATE:
            return "MUTEX_CREATE";
        case MSG_MUTEX_LOCK:
            return "MUTEX_LOCK";
        case MSG_MUTEX_UNLOCK:
            return "MUTEX_UNLOCK";
        case MSG_DEVOLVER_PROCESO:
            return "DEVOLVER_PROCESO";
        default:
            return "DESCONOCIDO";
    }
}
