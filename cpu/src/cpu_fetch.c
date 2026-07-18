#include "cpu_fetch.h"

#include <arpa/inet.h>
#include <stdlib.h>
#include <utils/protocolo.h>
#include <utils/sockets.h>

#include "cpu_logs.h"

/* FETCH = pedir a KM la instrucción del PID en el PC actual. KM conserva los
 * scripts; CPU sólo recibe una copia del texto que después va a decodificar. */
char* fetch_instruccion(int socket_memory, uint32_t pid, t_registros_cpu* registros, t_log* logger) {
    log_cpu_fetch(logger, pid, registros->pc);

    // PID y PC son uint32_t, por eso convierto ambos a byte order de red.
    t_payload_fetch_instruccion pedido = {
        .pid = htonl(pid),
        .pc = htonl(registros->pc)
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

    // deserializar_string crea otra reserva; puedo liberar el mensaje y devolver
    // el texto al ciclo, que se encarga de hacer free después del decode.
    char* instruccion = deserializar_string(respuesta->payload);
    free_mensaje(respuesta);

    return instruccion;
}
