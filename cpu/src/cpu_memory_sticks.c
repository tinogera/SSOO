#include "cpu_memory_sticks.h"

#include <arpa/inet.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <utils/protocolo.h>
#include <utils/sockets.h>

#define CPU_MEMORY_STICK_ID_MAX 4096U

static bool asegurar_capacidad(t_cpu_memory_sticks* registro, uint32_t id_memory_stick) {
    if (id_memory_stick > CPU_MEMORY_STICK_ID_MAX) {
        return false;
    }
    if ((size_t)id_memory_stick < registro->capacidad) {
        return true;
    }

    size_t nueva_capacidad = registro->capacidad == 0 ? 4 : registro->capacidad;
    while (nueva_capacidad <= (size_t)id_memory_stick) {
        nueva_capacidad *= 2;
    }

    int* sockets = realloc(registro->sockets, nueva_capacidad * sizeof(int));
    if (sockets == NULL) {
        return false;
    }
    for (size_t i = registro->capacidad; i < nueva_capacidad; i++) {
        sockets[i] = -1;
    }

    registro->sockets = sockets;
    registro->capacidad = nueva_capacidad;
    return true;
}

void cpu_memory_sticks_inicializar(
    t_cpu_memory_sticks* registro,
    int socket_kernel_memory,
    uint32_t cpu_id,
    t_log* logger
) {
    registro->socket_kernel_memory = socket_kernel_memory;
    registro->cpu_id = cpu_id;
    registro->sockets = NULL;
    registro->capacidad = 0;
    registro->logger = logger;
}

void cpu_memory_sticks_destruir(t_cpu_memory_sticks* registro) {
    if (registro == NULL) {
        return;
    }
    for (size_t i = 0; i < registro->capacidad; i++) {
        if (registro->sockets[i] >= 0) {
            close(registro->sockets[i]);
        }
    }
    free(registro->sockets);
    registro->sockets = NULL;
    registro->capacidad = 0;
}

static bool solicitar_endpoint(t_cpu_memory_sticks* registro, uint32_t id_memory_stick,
                               t_payload_memory_stick_endpoint* endpoint) {
    t_payload_solicitar_memory_stick pedido = {
        .id_memory_stick = htonl(id_memory_stick)
    };
    enviar_mensaje(registro->socket_kernel_memory, MSG_SOLICITAR_MEMORY_STICK,
                   &pedido, sizeof(pedido));

    t_mensaje* respuesta = recibir_mensaje(registro->socket_kernel_memory);
    if (respuesta == NULL) {
        if (registro->logger != NULL)
            log_error(registro->logger, "Kernel Memory cerro la conexion al resolver Memory Stick %u", id_memory_stick);
        return false;
    }

    bool ok = respuesta->op_code == MSG_MEMORY_STICK_ENDPOINT &&
              respuesta->payload_size == sizeof(*endpoint);
    if (ok) {
        memcpy(endpoint, respuesta->payload, sizeof(*endpoint));
    } else if (registro->logger != NULL) {
        log_debug(registro->logger, "Memory Stick %u todavia no esta disponible", id_memory_stick);
    }
    free_mensaje(respuesta);
    return ok;
}

static int conectar_endpoint(t_cpu_memory_sticks* registro, uint32_t id_memory_stick,
                             t_payload_memory_stick_endpoint* endpoint) {
    uint32_t id_recibido = ntohl(endpoint->id_memory_stick);
    uint32_t puerto = ntohl(endpoint->puerto);
    if (id_recibido != id_memory_stick || puerto == 0 || puerto > UINT16_MAX) {
        if (registro->logger != NULL)
            log_error(registro->logger, "Endpoint invalido para Memory Stick %u", id_memory_stick);
        return -1;
    }

    char ip[INET_ADDRSTRLEN];
    uint32_t ipv4 = endpoint->ipv4;
    if (inet_ntop(AF_INET, &ipv4, ip, sizeof(ip)) == NULL) {
        if (registro->logger != NULL)
            log_error(registro->logger, "IP invalida para Memory Stick %u", id_memory_stick);
        return -1;
    }

    int fd = conectar_a_servidor(ip, (int)puerto);
    if (fd < 0) {
        if (registro->logger != NULL)
            log_error(registro->logger, "No se pudo conectar a Memory Stick %u en %s:%u",
                      id_memory_stick, ip, puerto);
        return -1;
    }

    uint32_t cpu_id_n = htonl(registro->cpu_id);
    enviar_mensaje(fd, MSG_CPU_IDENTIFICACION, &cpu_id_n, sizeof(cpu_id_n));
    t_mensaje* handshake = recibir_mensaje(fd);
    bool ok = handshake != NULL && handshake->op_code == MSG_OK;
    free_mensaje(handshake);
    if (!ok) {
        close(fd);
        if (registro->logger != NULL)
            log_error(registro->logger, "Memory Stick %u rechazo la conexion", id_memory_stick);
        return -1;
    }
    return fd;
}

int cpu_memory_sticks_obtener_socket(t_cpu_memory_sticks* registro, uint32_t id_memory_stick) {
    if (registro == NULL || registro->socket_kernel_memory < 0) {
        return -1;
    }
    if ((size_t)id_memory_stick < registro->capacidad && registro->sockets[id_memory_stick] >= 0) {
        return registro->sockets[id_memory_stick];
    }

    t_payload_memory_stick_endpoint endpoint;
    if (!solicitar_endpoint(registro, id_memory_stick, &endpoint)) {
        return -1;
    }

    int fd = conectar_endpoint(registro, id_memory_stick, &endpoint);
    if (fd < 0) {
        return -1;
    }
    if (!asegurar_capacidad(registro, id_memory_stick)) {
        close(fd);
        return -1;
    }

    registro->sockets[id_memory_stick] = fd;
    if (registro->logger != NULL)
        log_info(registro->logger, "## Conectado a Memory Stick %u", id_memory_stick);
    return fd;
}
