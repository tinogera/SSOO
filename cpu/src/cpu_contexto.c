#include "cpu_contexto.h"

#include <arpa/inet.h>
#include <utils/protocolo.h>
#include <utils/sockets.h>

/*
 * El contexto permite pausar y reanudar procesos. KM guarda la versión estable;
 * CPU trabaja con una copia durante la ráfaga y la devuelve al terminar.
 */
bool restaurar_contexto_desde_memory(int socket_memory, uint32_t pid, t_contexto** contexto, t_registros_cpu* registros, t_log* logger) {
    // Para restaurar alcanza con el PID: KM ya tiene registros y segmentos.
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
    if (ctx == NULL) {
        log_error(logger, "Kernel Memory envio contexto corrupto para PID %u", pid);
        return false;
    }

    // Mantengo registros aparte porque son lo que execute modifica todo el tiempo.
    *registros = ctx->registros;
    *contexto = ctx;
    log_info(
        logger,
        "## PID: %u - Contexto restaurado - PC: %u - Segmentos: %u",
        pid,
        registros->pc,
        ctx->cant_segmentos
    );

    return true;
}

bool guardar_contexto_en_memory(int socket_memory, t_contexto* contexto, t_registros_cpu* registros, t_log* logger) {
    if (contexto == NULL) {
        log_error(logger, "No hay contexto para guardar en Kernel Memory");
        return false;
    }

    // Antes de serializar vuelco al contexto los valores finales de la ráfaga.
    contexto->registros = *registros;

    uint32_t size;
    // Se manda el contexto serializado completo. KM conserva la autoridad sobre
    // la tabla de segmentos y actualiza los registros para no pisar cambios suyos.
    void* buf = serializar_contexto(contexto, &size);
    enviar_mensaje(socket_memory, MSG_GUARDAR_CONTEXTO, buf, size);
    free(buf);

    t_mensaje* respuesta = recibir_mensaje(socket_memory);
    if (respuesta == NULL) {
        log_error(logger, "Kernel Memory cerro la conexion al guardar contexto del PID %u", contexto->pid);
        return false;
    }

    bool ok = respuesta->op_code == MSG_OK;
    if (!ok) {
        log_error(logger, "Kernel Memory rechazo guardar contexto del PID %u", contexto->pid);
    } else {
        log_info(
            logger,
            "## PID: %u - Contexto guardado - PC: %u - Segmentos: %u",
            contexto->pid,
            registros->pc,
            contexto->cant_segmentos
        );
    }

    free_mensaje(respuesta);
    return ok;
}

void liberar_contexto_cpu(t_contexto* contexto) {
    // El contexto restaurado fue reservado en heap, incluidos sus segmentos.
    free_contexto(contexto);
}
