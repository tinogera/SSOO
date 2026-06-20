#include "cpu_devolucion.h"

#include <arpa/inet.h>
#include <arpa/inet.h>
#include <utils/sockets.h>

const char* motivo_devolucion_to_string(t_motivo_devolucion_cpu motivo) {
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

bool devolver_proceso_a_scheduler(
    int socket_kernel,
    uint32_t pid,
    t_motivo_devolucion_cpu motivo,
    t_registros_cpu* registros,
    t_log* logger
) {
    t_payload_devolver_proceso payload = {
        .pid = htonl(pid),
        .motivo = htonl(motivo),
        .pc = htonl(registros->pc)
    };

    log_info(
        logger,
        "## PID: %u - Devolviendo proceso a Kernel Scheduler - Motivo: %s - PC: %u",
        pid,
        motivo_devolucion_to_string(motivo),
        registros->pc
    );

    enviar_mensaje(socket_kernel, MSG_DEVOLVER_PROCESO, &payload, sizeof(payload));
    return true;
}
