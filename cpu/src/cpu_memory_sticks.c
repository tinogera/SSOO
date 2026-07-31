#include "cpu_memory_sticks.h"

#include <arpa/inet.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <utils/protocolo.h>
#include <utils/sockets.h>

#define CPU_MEMORY_STICK_ID_MAX 4096U

static void inicializar_entrada(t_cpu_memory_stick_cache* entrada) {
    entrada->socket = -1;
    entrada->base_global = 0;
    entrada->tamanio = 0;
    entrada->rango_conocido = false;
}

static bool asegurar_capacidad(t_cpu_memory_sticks* registro, uint32_t id_memory_stick) {
    if (registro == NULL || id_memory_stick > CPU_MEMORY_STICK_ID_MAX) {
        return false;
    }
    if ((size_t) id_memory_stick < registro->capacidad) {
        return true;
    }

    size_t nueva_capacidad = registro->capacidad == 0 ? 4 : registro->capacidad;
    while (nueva_capacidad <= (size_t) id_memory_stick) {
        if (nueva_capacidad > SIZE_MAX / 2) {
            return false;
        }
        nueva_capacidad *= 2;
    }

    if (nueva_capacidad > SIZE_MAX / sizeof(t_cpu_memory_stick_cache)) {
        return false;
    }
    t_cpu_memory_stick_cache* sticks = realloc(
        registro->sticks,
        nueva_capacidad * sizeof(t_cpu_memory_stick_cache)
    );
    if (sticks == NULL) {
        return false;
    }

    for (size_t i = registro->capacidad; i < nueva_capacidad; i++) {
        inicializar_entrada(&sticks[i]);
    }
    registro->sticks = sticks;
    registro->capacidad = nueva_capacidad;
    return true;
}

void cpu_memory_sticks_inicializar(
    t_cpu_memory_sticks* registro,
    int socket_kernel_memory,
    uint32_t cpu_id,
    t_log* logger
) {
    if (registro == NULL) {
        return;
    }
    registro->socket_kernel_memory = socket_kernel_memory;
    registro->cpu_id = cpu_id;
    registro->sticks = NULL;
    registro->capacidad = 0;
    registro->logger = logger;
}

void cpu_memory_sticks_destruir(t_cpu_memory_sticks* registro) {
    if (registro == NULL) {
        return;
    }
    for (size_t i = 0; i < registro->capacidad; i++) {
        if (registro->sticks[i].socket >= 0) {
            close(registro->sticks[i].socket);
        }
    }
    free(registro->sticks);
    registro->sticks = NULL;
    registro->capacidad = 0;
}

bool cpu_memory_sticks_registrar_socket(
    t_cpu_memory_sticks* registro,
    uint32_t id_memory_stick,
    int socket
) {
    if (socket < 0 || !asegurar_capacidad(registro, id_memory_stick)) {
        return false;
    }

    t_cpu_memory_stick_cache* entrada = &registro->sticks[id_memory_stick];
    if (entrada->socket >= 0 && entrada->socket != socket) {
        close(entrada->socket);
    }
    entrada->socket = socket;
    return true;
}

static bool rango_valido(uint32_t base_global, uint32_t tamanio) {
    return tamanio > 0 && (uint64_t) base_global + tamanio <= (uint64_t) UINT32_MAX + 1u;
}

static bool endpoint_a_host(
    const t_payload_memory_stick_endpoint* endpoint_red,
    t_payload_memory_stick_endpoint* endpoint_host
) {
    endpoint_host->id_memory_stick = ntohl(endpoint_red->id_memory_stick);
    endpoint_host->ipv4 = endpoint_red->ipv4;
    endpoint_host->puerto = ntohl(endpoint_red->puerto);
    endpoint_host->base_global = ntohl(endpoint_red->base_global);
    endpoint_host->tamanio = ntohl(endpoint_red->tamanio);

    return endpoint_host->id_memory_stick <= CPU_MEMORY_STICK_ID_MAX &&
           endpoint_host->puerto <= UINT16_MAX &&
           rango_valido(endpoint_host->base_global, endpoint_host->tamanio);
}

static bool recibir_endpoint(
    t_cpu_memory_sticks* registro,
    t_payload_memory_stick_endpoint* endpoint_host
) {
    t_mensaje* respuesta = recibir_mensaje(registro->socket_kernel_memory);
    if (respuesta == NULL) {
        if (registro->logger != NULL) {
            log_error(registro->logger, "Kernel Memory cerro la conexion al resolver un Memory Stick");
        }
        return false;
    }

    bool ok = respuesta->op_code == MSG_MEMORY_STICK_ENDPOINT &&
              respuesta->payload_size == sizeof(t_payload_memory_stick_endpoint) &&
              respuesta->payload != NULL;
    if (ok) {
        t_payload_memory_stick_endpoint endpoint_red;
        memcpy(&endpoint_red, respuesta->payload, sizeof(endpoint_red));
        ok = endpoint_a_host(&endpoint_red, endpoint_host);
    }
    free_mensaje(respuesta);
    return ok;
}

static bool solicitar_endpoint_por_id(
    t_cpu_memory_sticks* registro,
    uint32_t id_memory_stick,
    t_payload_memory_stick_endpoint* endpoint
) {
    if (registro == NULL || registro->socket_kernel_memory < 0) {
        return false;
    }

    t_payload_solicitar_memory_stick pedido = {
        .id_memory_stick = htonl(id_memory_stick)
    };
    enviar_mensaje(
        registro->socket_kernel_memory,
        MSG_SOLICITAR_MEMORY_STICK,
        &pedido,
        sizeof(pedido)
    );
    if (!recibir_endpoint(registro, endpoint)) {
        return false;
    }
    return endpoint->id_memory_stick == id_memory_stick;
}

static bool solicitar_endpoint_por_direccion(
    t_cpu_memory_sticks* registro,
    uint32_t direccion_fisica,
    t_payload_memory_stick_endpoint* endpoint
) {
    if (registro == NULL || registro->socket_kernel_memory < 0) {
        return false;
    }

    t_payload_solicitar_memory_stick_direccion pedido = {
        .direccion_fisica = htonl(direccion_fisica)
    };
    enviar_mensaje(
        registro->socket_kernel_memory,
        MSG_SOLICITAR_MEMORY_STICK_DIRECCION,
        &pedido,
        sizeof(pedido)
    );
    if (!recibir_endpoint(registro, endpoint)) {
        return false;
    }
    return direccion_fisica >= endpoint->base_global &&
           (uint64_t) direccion_fisica < (uint64_t) endpoint->base_global + endpoint->tamanio;
}

static int conectar_endpoint(
    t_cpu_memory_sticks* registro,
    const t_payload_memory_stick_endpoint* endpoint
) {
    if (endpoint->puerto == 0) {
        return -1;
    }

    char ip[INET_ADDRSTRLEN];
    if (inet_ntop(AF_INET, &endpoint->ipv4, ip, sizeof(ip)) == NULL) {
        return -1;
    }

    int socket = conectar_a_servidor(ip, (int) endpoint->puerto);
    if (socket < 0) {
        if (registro->logger != NULL) {
            log_error(
                registro->logger,
                "No se pudo conectar a Memory Stick %u en %s:%u",
                endpoint->id_memory_stick,
                ip,
                endpoint->puerto
            );
        }
        return -1;
    }

    uint32_t cpu_id_n = htonl(registro->cpu_id);
    enviar_mensaje(socket, MSG_CPU_IDENTIFICACION, &cpu_id_n, sizeof(cpu_id_n));
    t_mensaje* respuesta = recibir_mensaje(socket);
    bool ok = respuesta != NULL && respuesta->op_code == MSG_OK;
    free_mensaje(respuesta);
    if (!ok) {
        close(socket);
        return -1;
    }
    return socket;
}

static bool cachear_endpoint(
    t_cpu_memory_sticks* registro,
    const t_payload_memory_stick_endpoint* endpoint,
    t_cpu_memory_stick_resuelto* resultado
) {
    if (!asegurar_capacidad(registro, endpoint->id_memory_stick)) {
        return false;
    }

    t_cpu_memory_stick_cache* entrada = &registro->sticks[endpoint->id_memory_stick];
    entrada->base_global = endpoint->base_global;
    entrada->tamanio = endpoint->tamanio;
    entrada->rango_conocido = true;

    if (entrada->socket < 0) {
        entrada->socket = conectar_endpoint(registro, endpoint);
        if (entrada->socket < 0) {
            return false;
        }
        if (registro->logger != NULL) {
            log_info(registro->logger, "## Conectado a Memory Stick %u", endpoint->id_memory_stick);
        }
    }

    resultado->id_memory_stick = endpoint->id_memory_stick;
    resultado->base_global = entrada->base_global;
    resultado->tamanio = entrada->tamanio;
    resultado->socket = entrada->socket;
    resultado->rango_conocido = true;
    return true;
}

static void copiar_cache(
    uint32_t id_memory_stick,
    const t_cpu_memory_stick_cache* entrada,
    t_cpu_memory_stick_resuelto* resultado
) {
    resultado->id_memory_stick = id_memory_stick;
    resultado->base_global = entrada->base_global;
    resultado->tamanio = entrada->tamanio;
    resultado->socket = entrada->socket;
    resultado->rango_conocido = entrada->rango_conocido;
}

bool cpu_memory_sticks_resolver_id(
    t_cpu_memory_sticks* registro,
    uint32_t id_memory_stick,
    t_cpu_memory_stick_resuelto* resultado
) {
    if (registro == NULL || resultado == NULL || id_memory_stick > CPU_MEMORY_STICK_ID_MAX) {
        return false;
    }

    t_cpu_memory_stick_cache* cache = NULL;
    if ((size_t) id_memory_stick < registro->capacidad) {
        cache = &registro->sticks[id_memory_stick];
        if (cache->socket >= 0 && cache->rango_conocido) {
            copiar_cache(id_memory_stick, cache, resultado);
            return true;
        }
    }

    t_payload_memory_stick_endpoint endpoint;
    if (solicitar_endpoint_por_id(registro, id_memory_stick, &endpoint)) {
        return cachear_endpoint(registro, &endpoint, resultado);
    }

    // Compatibilidad con configs anteriores: si KM no publica endpoints, el
    // socket preconectado sigue sirviendo para accesos que no se fragmentan.
    if (cache != NULL && cache->socket >= 0) {
        copiar_cache(id_memory_stick, cache, resultado);
        return true;
    }
    return false;
}

bool cpu_memory_sticks_resolver_direccion(
    t_cpu_memory_sticks* registro,
    uint32_t direccion_fisica,
    t_cpu_memory_stick_resuelto* resultado
) {
    if (registro == NULL || resultado == NULL) {
        return false;
    }

    for (size_t i = 0; i < registro->capacidad; i++) {
        t_cpu_memory_stick_cache* entrada = &registro->sticks[i];
        if (entrada->socket >= 0 && entrada->rango_conocido &&
            direccion_fisica >= entrada->base_global &&
            (uint64_t) direccion_fisica < (uint64_t) entrada->base_global + entrada->tamanio) {
            copiar_cache((uint32_t) i, entrada, resultado);
            return true;
        }
    }

    t_payload_memory_stick_endpoint endpoint;
    if (!solicitar_endpoint_por_direccion(registro, direccion_fisica, &endpoint)) {
        return false;
    }
    return cachear_endpoint(registro, &endpoint, resultado);
}

void cpu_memory_sticks_invalidar_socket(
    t_cpu_memory_sticks* registro,
    uint32_t id_memory_stick,
    int socket
) {
    if (registro == NULL || (size_t) id_memory_stick >= registro->capacidad) {
        return;
    }
    t_cpu_memory_stick_cache* entrada = &registro->sticks[id_memory_stick];
    if (entrada->socket == socket) {
        close(entrada->socket);
        entrada->socket = -1;
    }
}
