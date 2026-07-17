#ifndef CPU_HANDSHAKE_H_
#define CPU_HANDSHAKE_H_

#include <stdbool.h>
#include <utils/sockets.h>

/*
 * Evalúa la respuesta de un handshake de identificación (KS, KM, MS).
 * No hace logging ni toca el socket: solo decide si el handshake fue exitoso.
 * `respuesta` puede ser NULL (el peer cerró la conexión antes de responder,
 * o nunca llegó a responder). Devuelve true únicamente si respuesta != NULL
 * y respuesta->op_code == MSG_OK.
 */
bool handshake_exitoso(t_mensaje* respuesta);

#endif
