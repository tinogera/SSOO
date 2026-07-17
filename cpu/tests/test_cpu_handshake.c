#include <cspecs/cspec.h>
#include <stdlib.h>
#include <utils/sockets.h>
#include <utils/protocolo.h>
#include "cpu_handshake.h"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static t_mensaje* crear_msg(uint32_t op_code) {
    t_mensaje* msg = malloc(sizeof(t_mensaje));
    msg->op_code      = op_code;
    msg->payload_size = 0;
    msg->payload      = NULL;
    return msg;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

context(cpu_handshake) {

    describe("handshake_exitoso") {

        it("devuelve true con MSG_OK") {
            t_mensaje* msg = crear_msg(MSG_OK);
            should_bool(handshake_exitoso(msg)) be truthy;
            free_mensaje(msg);
        } end

        it("devuelve false con NULL (peer cerro la conexion)") {
            should_bool(handshake_exitoso(NULL)) not be truthy;
        } end

        it("devuelve false con MSG_ERROR") {
            t_mensaje* msg = crear_msg(MSG_ERROR);
            should_bool(handshake_exitoso(msg)) not be truthy;
            free_mensaje(msg);
        } end

        it("devuelve false con un op_code inesperado cualquiera") {
            t_mensaje* msg = crear_msg(MSG_RESPUESTA_INSTRUCCION);
            should_bool(handshake_exitoso(msg)) not be truthy;
            free_mensaje(msg);
        } end

    } end

}
