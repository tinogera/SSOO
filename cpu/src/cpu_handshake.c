#include "cpu_handshake.h"

#include <utils/protocolo.h>

// Uso la misma regla para KS, KM y MS: conexión aceptada únicamente con MSG_OK.
bool handshake_exitoso(t_mensaje* respuesta) {
    return respuesta != NULL && respuesta->op_code == MSG_OK;
}
