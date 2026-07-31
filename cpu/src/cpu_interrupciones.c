#include "cpu_interrupciones.h"

#include <arpa/inet.h>
#include <errno.h>
#include <sys/select.h>
#include <utils/sockets.h>

#include "cpu_logs.h"

const char* motivo_interrupcion_to_string(t_motivo_interrupcion_cpu motivo) {
    switch (motivo) {
        case MOTIVO_INTERRUPCION_QUANTUM:
            return "QUANTUM";
        case MOTIVO_INTERRUPCION_DESALOJO:
            return "DESALOJO";
        default:
            return "DESCONOCIDO";
    }
}

t_resultado_interrupcion_cpu recibir_interrupcion_cpu(
    int socket_kernel,
    uint32_t pid_en_ejecucion,
    t_interrupcion_cpu* interrupcion,
    t_log* logger
) {
    if (interrupcion == NULL) {
        log_error(logger, "No hay destino para guardar la interrupcion");
        return CPU_INTERRUPCION_ERROR;
    }

    t_mensaje* mensaje = recibir_mensaje(socket_kernel);
    if (mensaje == NULL) {
        log_error(logger, "Kernel Scheduler cerro la conexion esperando interrupcion");
        return CPU_INTERRUPCION_ERROR;
    }

    if (mensaje->op_code != MSG_INTERRUPCION_CPU) {
        log_error(logger, "Mensaje inesperado esperando interrupcion: op_code=%u", mensaje->op_code);
        free_mensaje(mensaje);
        return CPU_INTERRUPCION_ERROR;
    }

    if (mensaje->payload == NULL || mensaje->payload_size != sizeof(t_payload_interrupcion_cpu)) {
        log_error(logger, "Payload invalido para interrupcion CPU");
        free_mensaje(mensaje);
        return CPU_INTERRUPCION_ERROR;
    }

    const t_payload_interrupcion_cpu* payload = mensaje->payload;
    uint32_t pid = ntohl(payload->pid);
    t_motivo_interrupcion_cpu motivo = (t_motivo_interrupcion_cpu) ntohl(payload->motivo);

    if (motivo != MOTIVO_INTERRUPCION_QUANTUM && motivo != MOTIVO_INTERRUPCION_DESALOJO) {
        log_error(logger, "Motivo invalido en interrupcion de CPU: %u", (uint32_t) motivo);
        free_mensaje(mensaje);
        return CPU_INTERRUPCION_ERROR;
    }

    if (pid != pid_en_ejecucion) {
        log_warning(logger,
                    "Se descarta interrupcion atrasada para PID %u; actualmente ejecuta PID %u",
                    pid, pid_en_ejecucion);
        free_mensaje(mensaje);
        return CPU_INTERRUPCION_SIN_MENSAJE;
    }

    interrupcion->pid = pid;
    interrupcion->motivo = motivo;
    log_cpu_interrupcion(logger);
    log_debug(logger, "Interrupcion recibida - PID: %u - Motivo: %s",
              pid, motivo_interrupcion_to_string(motivo));

    free_mensaje(mensaje);
    return CPU_INTERRUPCION_RECIBIDA;
}

t_resultado_interrupcion_cpu recibir_interrupcion_cpu_si_hay(
    int socket_kernel,
    uint32_t pid_en_ejecucion,
    t_interrupcion_cpu* interrupcion,
    t_log* logger
) {
    int resultado;

    do {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(socket_kernel, &read_fds);

        struct timeval timeout = {
            .tv_sec = 0,
            .tv_usec = 0
        };

        resultado = select(socket_kernel + 1, &read_fds, NULL, NULL, &timeout);
        if (resultado > 0 && FD_ISSET(socket_kernel, &read_fds)) {
            return recibir_interrupcion_cpu(socket_kernel, pid_en_ejecucion, interrupcion, logger);
        }
    } while (resultado < 0 && errno == EINTR);

    if (resultado < 0) {
        log_error(logger, "Error consultando interrupciones de Kernel Scheduler");
        return CPU_INTERRUPCION_ERROR;
    }

    return CPU_INTERRUPCION_SIN_MENSAJE;
}
