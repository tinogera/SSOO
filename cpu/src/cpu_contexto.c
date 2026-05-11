#include "cpu_contexto.h"

#include <utils/protocolo.h>
#include <utils/sockets.h>

static void copiar_contexto_a_registros(t_contexto_ejecucion_cpu* contexto, t_registros_cpu* registros) {
    registros->pc = contexto->pc;
    registros->ax = contexto->ax;
    registros->bx = contexto->bx;
    registros->cx = contexto->cx;
    registros->dx = contexto->dx;
    registros->eax = contexto->eax;
    registros->ebx = contexto->ebx;
    registros->ecx = contexto->ecx;
    registros->edx = contexto->edx;
    registros->si = contexto->si;
    registros->di = contexto->di;
}

static t_contexto_ejecucion_cpu contexto_desde_registros(t_registros_cpu* registros) {
    return (t_contexto_ejecucion_cpu) {
        .pc = registros->pc,
        .ax = registros->ax,
        .bx = registros->bx,
        .cx = registros->cx,
        .dx = registros->dx,
        .eax = registros->eax,
        .ebx = registros->ebx,
        .ecx = registros->ecx,
        .edx = registros->edx,
        .si = registros->si,
        .di = registros->di
    };
}

bool restaurar_contexto_desde_memory(int socket_memory, uint32_t pid, t_registros_cpu* registros, t_log* logger) {
    t_payload_obtener_contexto pedido = {
        .pid = pid
    };

    enviar_mensaje(socket_memory, MSG_OBTENER_CONTEXTO, &pedido, sizeof(pedido));

    t_mensaje* respuesta = recibir_mensaje(socket_memory);
    if (respuesta == NULL) {
        log_error(logger, "Kernel Memory cerro la conexion al pedir contexto del PID %u", pid);
        return false;
    }

    bool ok = respuesta->op_code == MSG_RESPUESTA_CONTEXTO &&
              respuesta->payload_size == sizeof(t_payload_contexto_cpu);

    if (!ok) {
        log_error(logger, "Kernel Memory no envio contexto valido para PID %u", pid);
        free_mensaje(respuesta);
        return false;
    }

    t_payload_contexto_cpu* payload = (t_payload_contexto_cpu*) respuesta->payload;
    copiar_contexto_a_registros(&payload->contexto, registros);
    log_info(logger, "## PID: %u - Contexto restaurado - PC: %u", pid, registros->pc);

    free_mensaje(respuesta);
    return true;
}

bool guardar_contexto_en_memory(int socket_memory, uint32_t pid, t_registros_cpu* registros, t_log* logger) {
    t_payload_contexto_cpu payload = {
        .pid = pid,
        .contexto = contexto_desde_registros(registros)
    };

    enviar_mensaje(socket_memory, MSG_GUARDAR_CONTEXTO, &payload, sizeof(payload));

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
