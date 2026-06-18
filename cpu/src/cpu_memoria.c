#include "cpu_memoria.h"

#include <stdlib.h>
#include <string.h>

#include <utils/protocolo.h>
#include <utils/sockets.h>

bool memoria_write(int socket_ms, uint32_t direccion, uint32_t tamanio, void* datos) {

    uint32_t payload_size = 8 + tamanio;
    void* payload = malloc(payload_size);

    memcpy(payload, &direccion, 4);
    memcpy(payload + 4, &tamanio, 4);
    memcpy(payload + 8, datos, tamanio);

    enviar_mensaje(socket_ms, MSG_MEMORY_WRITE, payload, payload_size);

    free(payload);

    t_mensaje* respuesta = recibir_mensaje(socket_ms);

    if(respuesta == NULL) {
        return false;
    }

    bool ok = respuesta->op_code == MSG_OK;

    free_mensaje(respuesta);

    return ok;
}

bool memoria_read(int socket_ms, uint32_t direccion, uint32_t tamanio, void* destino) {

    uint8_t payload[8];

    memcpy(payload, &direccion, 4);
    memcpy(payload + 4, &tamanio, 4);

    enviar_mensaje(socket_ms, MSG_MEMORY_READ, payload, sizeof(payload));

    t_mensaje* respuesta = recibir_mensaje(socket_ms);

    if(respuesta == NULL) {
        return false;
    }

    if(respuesta->op_code != MSG_MEMORY_READ_RESPUESTA) {
        free_mensaje(respuesta);
        return false;
    }

    memcpy(destino, respuesta->payload, tamanio);

    free_mensaje(respuesta);

    return true;
}