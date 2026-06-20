#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <commons/log.h>
#include <commons/config.h>
#include <unistd.h>
#include <utils/sockets.h>
#include <utils/protocolo.h>

#include "cpu_ciclo.h"
#include "cpu_contexto.h"
#include "cpu_devolucion.h"
#include "cpu_dispatch.h"
#include "cpu_registros.h"

int main(int argc, char* argv[]) {

    if (argc < 3) {
        printf("Uso: ./cpu [CONFIG] [ID]\n");
        return 1;
    }

    char* config_path = argv[1];
    char* cpu_id = argv[2];

    t_config* config = config_create(config_path);
    t_log* logger = log_create("cpu.log", cpu_id, 1, LOG_LEVEL_INFO);
    t_registros_cpu registros;
    inicializar_registros_cpu(&registros);

    log_info(logger, "CPU %s iniciada", cpu_id);
    log_info(logger, "Registros CPU inicializados - PC: %u", registros.pc);

    char* ip_kernel = config_get_string_value(config, "IP_KERNEL");
    int puerto_kernel = config_get_int_value(config, "PUERTO_KERNEL");

    char* ip_memory = config_get_string_value(config, "IP_MEMORY");
    int puerto_memory = config_get_int_value(config, "PUERTO_MEMORY");
    uint32_t tamanio_max_segmento = 256;
    if (config_has_property(config, "SEGMENT_MAX_SIZE")) {
        tamanio_max_segmento = (uint32_t) config_get_int_value(config, "SEGMENT_MAX_SIZE");
    }

     // CONEXION A KERNEL SCHEDULER
    int socket_kernel = conectar_a_servidor(ip_kernel, puerto_kernel);
    if (socket_kernel == -1) {
        log_error(logger, "No se pudo conectar a Kernel Scheduler");
    } else {
        log_info(logger, "Conectado a Kernel Scheduler");

        // HANDSHAKE CPU -> KS
        uint32_t size;
        void* payload = serializar_string(cpu_id, &size);
        enviar_mensaje(socket_kernel, MSG_CPU_IDENTIFICACION, payload, size);
        free(payload);

        t_mensaje* respuesta_kernel = recibir_mensaje(socket_kernel);
        if (respuesta_kernel == NULL) {
            log_error(logger, "Kernel Scheduler cerro la conexion durante la identificacion");
            socket_kernel = -1;
        } else if (respuesta_kernel->op_code == MSG_OK) {
            log_info(logger, "## Conectado a Kernel Scheduler");
        } else {
            log_error(logger, "Kernel Scheduler rechazo la identificacion");
            socket_kernel = -1;
        }

        free_mensaje(respuesta_kernel);
    }

    // CONEXION A KERNEL MEMORY
    int socket_memory = conectar_a_servidor(ip_memory, puerto_memory);
    if (socket_memory == -1) {
        log_error(logger, "No se pudo conectar a Kernel Memory");
    } else {
        log_info(logger, "Conectado a Kernel Memory");

        // HANDSHAKE CPU -> Kernel Memory
        enviar_mensaje(socket_memory, MSG_CPU_IDENTIFICACION, NULL, 0);

        t_mensaje* respuesta = recibir_mensaje(socket_memory);
        if (respuesta == NULL) {
            log_error(logger, "Kernel Memory cerro la conexion durante el handshake");
        } else if (respuesta->op_code == MSG_OK) {
            log_info(logger, "## Conectado a Kernel Memory");
        } else {
            log_error(logger, "Kernel Memory rechazo la conexion");
        }

        free_mensaje(respuesta);
    }

    int socket_memoria_usuario = -1;
    if (config_has_property(config, "IP_MEMORY_STICK") && config_has_property(config, "PUERTO_MEMORY_STICK")) {
        char* ip_memory_stick = config_get_string_value(config, "IP_MEMORY_STICK");
        int puerto_memory_stick = config_get_int_value(config, "PUERTO_MEMORY_STICK");

        socket_memoria_usuario = conectar_a_servidor(ip_memory_stick, puerto_memory_stick);
        if (socket_memoria_usuario == -1) {
            log_error(logger, "No se pudo conectar a Memory Stick");
        } else {
            uint32_t cpu_id_n = htonl((uint32_t) atoi(cpu_id));
            enviar_mensaje(socket_memoria_usuario, MSG_CPU_IDENTIFICACION, &cpu_id_n, sizeof(cpu_id_n));

            t_mensaje* respuesta_ms = recibir_mensaje(socket_memoria_usuario);
            if (respuesta_ms == NULL || respuesta_ms->op_code != MSG_OK) {
                log_error(logger, "Memory Stick rechazo la conexion de CPU");
                close(socket_memoria_usuario);
                socket_memoria_usuario = -1;
            } else {
                log_info(logger, "## Conectado a Memory Stick");
            }

            free_mensaje(respuesta_ms);
        }
    }

    // Esperar procesos despachados por Kernel Scheduler
    while(socket_kernel != -1) {
        uint32_t pid;
        if (!recibir_proceso_a_ejecutar(socket_kernel, &pid, logger)) {
            break;
        }

        t_contexto* contexto = NULL;
        if (!restaurar_contexto_desde_memory(socket_memory, pid, &contexto, &registros, logger)) {
            log_error(logger, "No se pudo restaurar contexto para PID %u", pid);
            break;
        }

        t_resultado_ciclo_cpu resultado_ciclo = ejecutar_ciclo_proceso(
            socket_kernel,
            socket_memory,
            socket_memoria_usuario,
            pid,
            contexto,
            &registros,
            tamanio_max_segmento,
            logger
        );
        t_motivo_devolucion_cpu motivo_devolucion;
        if (resultado_ciclo == CPU_CICLO_SYSCALL) {
            motivo_devolucion = MOTIVO_DEVOLUCION_SYSCALL;
        } else if (resultado_ciclo == CPU_CICLO_EXIT) {
            motivo_devolucion = MOTIVO_DEVOLUCION_EXIT;
        } else if (resultado_ciclo == CPU_CICLO_INTERRUPCION) {
            motivo_devolucion = MOTIVO_DEVOLUCION_INTERRUPCION;
        } else if (resultado_ciclo == CPU_CICLO_SEG_FAULT) {
            motivo_devolucion = MOTIVO_DEVOLUCION_SEG_FAULT;
        } else {
            motivo_devolucion = MOTIVO_DEVOLUCION_ERROR;
        }

        if (!guardar_contexto_en_memory(socket_memory, contexto, &registros, logger)) {
            motivo_devolucion = MOTIVO_DEVOLUCION_ERROR;
        }
        liberar_contexto_cpu(contexto);

        devolver_proceso_a_scheduler(socket_kernel, pid, motivo_devolucion, &registros, logger);

        if (motivo_devolucion == MOTIVO_DEVOLUCION_ERROR) {
            log_error(logger, "Fallo el ciclo de instruccion para PID %u", pid);
            break;
        }
    }

    close(socket_kernel);
    close(socket_memory);
    if (socket_memoria_usuario != -1) {
        close(socket_memoria_usuario);
    }

    log_destroy(logger);
    config_destroy(config);

    return 0;
}
