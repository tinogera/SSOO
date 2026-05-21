#include "cpu_interrupciones.h"

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

bool recibir_interrupcion_cpu(int socket_kernel, t_interrupcion_cpu* interrupcion, t_log* logger) {
    t_mensaje* mensaje = recibir_mensaje(socket_kernel);
    if (mensaje == NULL) {
        log_error(logger, "Kernel Scheduler cerro la conexion esperando interrupcion");
        return false;
    }

    if (mensaje->op_code != MSG_INTERRUPCION_CPU) {
        log_error(logger, "Mensaje inesperado esperando interrupcion: op_code=%u", mensaje->op_code);
        free_mensaje(mensaje);
        return false;
    }

    if (mensaje->payload_size != sizeof(t_payload_interrupcion_cpu)) {
        log_error(logger, "Payload invalido para interrupcion CPU");
        free_mensaje(mensaje);
        return false;
    }

    t_payload_interrupcion_cpu* payload = (t_payload_interrupcion_cpu*) mensaje->payload;
    interrupcion->pid = payload->pid;
    interrupcion->motivo = (t_motivo_interrupcion_cpu) payload->motivo;

    log_cpu_interrupcion(logger);

    free_mensaje(mensaje);
    return true;
}

bool recibir_interrupcion_cpu_si_hay(int socket_kernel, t_interrupcion_cpu* interrupcion, t_log* logger) {
    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(socket_kernel, &read_fds);

    struct timeval timeout = {
        .tv_sec = 0,
        .tv_usec = 0
    };

    int resultado = select(socket_kernel + 1, &read_fds, NULL, NULL, &timeout);
    if (resultado < 0) {
        log_error(logger, "Error consultando interrupciones de Kernel Scheduler");
        return false;
    }

    if (resultado == 0 || !FD_ISSET(socket_kernel, &read_fds)) {
        return false;
    }

    return recibir_interrupcion_cpu(socket_kernel, interrupcion, logger);
}
