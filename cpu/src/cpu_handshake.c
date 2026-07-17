#include "cpu_handshake.h"

#include <utils/protocolo.h>

bool handshake_exitoso(t_mensaje* respuesta) {
    return respuesta != NULL && respuesta->op_code == MSG_OK;
}
