#ifndef CPU_HANDSHAKE_H_
#define CPU_HANDSHAKE_H_

#include <stdbool.h>
#include <utils/sockets.h>

/*
 * Evalúa la respuesta del handshake de KS, KM o MS. Esta función no recibe ni
 * libera el mensaje: sólo devuelve true si existe y su opcode es MSG_OK.
 */
bool handshake_exitoso(t_mensaje* respuesta);

#endif
