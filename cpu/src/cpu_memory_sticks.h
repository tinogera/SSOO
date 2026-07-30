#ifndef CPU_MEMORY_STICKS_H_
#define CPU_MEMORY_STICKS_H_

#include <stddef.h>
#include <stdint.h>
#include <commons/log.h>

typedef struct {
    int       socket_kernel_memory;
    uint32_t  cpu_id;
    int*      sockets;
    size_t    capacidad;
    t_log*    logger;
} t_cpu_memory_sticks;

void cpu_memory_sticks_inicializar(
    t_cpu_memory_sticks* registro,
    int socket_kernel_memory,
    uint32_t cpu_id,
    t_log* logger
);

void cpu_memory_sticks_destruir(t_cpu_memory_sticks* registro);

// Devuelve una conexión ya existente o consulta a Kernel Memory y conecta el
// stick de forma perezosa. Retorna -1 si el stick todavía no está disponible.
int cpu_memory_sticks_obtener_socket(t_cpu_memory_sticks* registro, uint32_t id_memory_stick);

#endif
