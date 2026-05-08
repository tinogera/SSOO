#include "cpu_fetch.h"

#include <stdlib.h>
#include <utils/protocolo.h>
#include <utils/sockets.h>

#include "cpu_logs.h"

char* fetch_instruccion(int socket_memory, uint32_t pid, t_registros_cpu* registros, t_log* logger) {
    log_cpu_fetch(logger, pid, registros->pc);

    t_payload_fetch_instruccion pedido = {
        .pid = pid,
        .pc = registros->pc
    };

    enviar_mensaje(socket_memory, MSG_FETCH_INSTRUCCION, &pedido, sizeof(pedido));

    t_mensaje* respuesta = recibir_mensaje(socket_memory);
    if (respuesta == NULL) {
        log_error(logger, "Kernel Memory cerro la conexion durante FETCH");
        return NULL;
    }

    if (respuesta->op_code != MSG_RESPUESTA_INSTRUCCION) {
        log_error(logger, "Respuesta inesperada de Kernel Memory durante FETCH: op_code=%u", respuesta->op_code);
        free_mensaje(respuesta);
        return NULL;
    }

    char* instruccion = deserializar_string(respuesta->payload);
    free_mensaje(respuesta);

    return instruccion;
}
