#include "cpu_contexto.h"

#include <arpa/inet.h>
#include <utils/protocolo.h>
#include <utils/sockets.h>

bool restaurar_contexto_desde_memory(int socket_memory, uint32_t pid, t_registros_cpu* registros, t_log* logger) {
    uint32_t pid_n = htonl(pid);
    enviar_mensaje(socket_memory, MSG_RESTAURAR_CONTEXTO, &pid_n, sizeof(pid_n));

    t_mensaje* respuesta = recibir_mensaje(socket_memory);
    if (respuesta == NULL) {
        log_error(logger, "Kernel Memory cerro la conexion al pedir contexto del PID %u", pid);
        return false;
    }

    if (respuesta->op_code != MSG_RESTAURAR_CONTEXTO) {
        log_error(logger, "Kernel Memory no envio contexto valido para PID %u (op_code=%u)", pid, respuesta->op_code);
        free_mensaje(respuesta);
        return false;
    }

    t_contexto* ctx = deserializar_contexto(respuesta->payload, respuesta->payload_size);
    free_mensaje(respuesta);

    *registros = ctx->registros;
    log_info(logger, "## PID: %u - Contexto restaurado - PC: %u", pid, registros->pc);

    free_contexto(ctx);
    return true;
}

bool guardar_contexto_en_memory(int socket_memory, uint32_t pid, t_registros_cpu* registros, t_log* logger) {
    t_contexto ctx = {
        .pid            = pid,
        .registros      = *registros,
        .cant_segmentos = 0,
        .segmentos      = NULL,
    };

    uint32_t size;
    void* buf = serializar_contexto(&ctx, &size);
    enviar_mensaje(socket_memory, MSG_GUARDAR_CONTEXTO, buf, size);
    free(buf);

    t_mensaje* respuesta = recibir_mensaje(socket_memory);
    if (respuesta == NULL) {
        log_error(logger, "Kernel Memory cerro la conexion al guardar contexto del PID %u", pid);
        return false;
    }

    bool ok = respuesta->op_code == MSG_OK;
    if (!ok) {
        log_error(logger, "Kernel Memory rechazo guardar contexto del PID %u", pid);
    } else {
        log_info(logger, "## PID: %u - Contexto guardado - PC: %u", pid, registros->pc);
    }

    free_mensaje(respuesta);
    return ok;
}
