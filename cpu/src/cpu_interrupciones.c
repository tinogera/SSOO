#include "cpu_interrupciones.h"

#include <arpa/inet.h>
#include <sys/select.h>
#include <utils/sockets.h>

#include "cpu_logs.h"

/*
 * La interrupción no aparece sola: KS la manda por fin de quantum, llegada de
 * una prioridad mayor o alguna coordinación como compactación. CPU la consulta
 * entre instrucciones para no dejar una operación a medias.
 */
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

    // Ojo: KS manda estos uint32_t en byte order de red. En esta versión se
    // copian directo; si necesito usar sus valores tengo que aplicar ntohl.
    // Hoy alcanza la llegada del mensaje para cortar la ráfaga.
    interrupcion->pid = payload->pid;
    interrupcion->motivo = (t_motivo_interrupcion_cpu) payload->motivo;

    log_cpu_interrupcion(logger);
    log_debug(logger, "Interrupción recibida - PID: %u - Motivo: %s",
              ntohl(payload->pid), motivo_interrupcion_to_string((t_motivo_interrupcion_cpu) ntohl(payload->motivo)));

    free_mensaje(mensaje);
    return true;
}

bool recibir_interrupcion_cpu_si_hay(int socket_kernel, t_interrupcion_cpu* interrupcion, t_log* logger) {
    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(socket_kernel, &read_fds);

    // Timeout cero vuelve a select no bloqueante: si no hay interrupción sigo
    // ejecutando enseguida y no freno el ciclo esperando a KS.
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
