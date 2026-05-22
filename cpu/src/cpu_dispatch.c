#include "cpu_dispatch.h"

#include <arpa/inet.h>
#include <utils/protocolo.h>
#include <utils/sockets.h>

bool recibir_proceso_a_ejecutar(int socket_kernel, uint32_t* pid, t_log* logger) {
    t_mensaje* mensaje = recibir_mensaje(socket_kernel);
    if (mensaje == NULL) {
        log_error(logger, "Kernel Scheduler cerro la conexion esperando proceso a ejecutar");
        return false;
    }

    if (mensaje->op_code != MSG_DESPACHAR_PROCESO) {
        log_error(logger, "Mensaje inesperado esperando proceso a ejecutar: op_code=%u", mensaje->op_code);
        free_mensaje(mensaje);
        return false;
    }

    if (mensaje->payload_size != sizeof(t_payload_despachar_proceso)) {
        log_error(logger, "Payload invalido al recibir proceso a ejecutar");
        free_mensaje(mensaje);
        return false;
    }

    t_payload_despachar_proceso* payload = (t_payload_despachar_proceso*) mensaje->payload;
    *pid = ntohl(payload->pid);

    log_info(logger, "## PID: %u - Proceso recibido de Kernel Scheduler", *pid);

    free_mensaje(mensaje);
    return true;
}
