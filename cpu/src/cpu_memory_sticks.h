#ifndef CPU_MEMORY_STICKS_H_
#define CPU_MEMORY_STICKS_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <commons/log.h>

typedef struct {
    int socket;
    uint32_t base_global;
    uint32_t tamanio;
    bool rango_conocido;
} t_cpu_memory_stick_cache;

typedef struct {
    int socket_kernel_memory;
    uint32_t cpu_id;
    t_cpu_memory_stick_cache* sticks;
    size_t capacidad;
    t_log* logger;
} t_cpu_memory_sticks;

typedef struct {
    uint32_t id_memory_stick;
    uint32_t base_global;
    uint32_t tamanio;
    int socket;
    bool rango_conocido;
} t_cpu_memory_stick_resuelto;

void cpu_memory_sticks_inicializar(
    t_cpu_memory_sticks* registro,
    int socket_kernel_memory,
    uint32_t cpu_id,
    t_log* logger
);

void cpu_memory_sticks_destruir(t_cpu_memory_sticks* registro);

// Permite seguir usando configuraciones anteriores mientras Kernel Memory
// publica los endpoints de manera dinamica. El registro toma propiedad del fd.
bool cpu_memory_sticks_registrar_socket(
    t_cpu_memory_sticks* registro,
    uint32_t id_memory_stick,
    int socket
);

// Resuelve por ID el primer stick indicado por la tabla de segmentos.
bool cpu_memory_sticks_resolver_id(
    t_cpu_memory_sticks* registro,
    uint32_t id_memory_stick,
    t_cpu_memory_stick_resuelto* resultado
);

// Resuelve el stick que contiene una direccion fisica global. Se usa al
// continuar una operacion que atraviesa el limite fisico de un dispositivo.
bool cpu_memory_sticks_resolver_direccion(
    t_cpu_memory_sticks* registro,
    uint32_t direccion_fisica,
    t_cpu_memory_stick_resuelto* resultado
);

void cpu_memory_sticks_invalidar_socket(
    t_cpu_memory_sticks* registro,
    uint32_t id_memory_stick,
    int socket
);

#endif
