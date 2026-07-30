#ifndef CPU_CICLO_H_
#define CPU_CICLO_H_

#include <stdint.h>
#include <commons/log.h>

#include "cpu_registros.h"
#include "cpu_memory_sticks.h"

// Resultado de una ráfaga completa, no de una sola instrucción. main lo
// convierte después al motivo de devolución que entiende KS.
typedef enum {
    CPU_CICLO_OK, // Está reservado; el while actual siempre termina por algún motivo de corte.
    CPU_CICLO_ERROR_FETCH,
    CPU_CICLO_ERROR_DECODE,
    CPU_CICLO_ERROR_EXECUTE,
    CPU_CICLO_SYSCALL,
    CPU_CICLO_EXIT,
    CPU_CICLO_INTERRUPCION,
    CPU_CICLO_SEG_FAULT
} t_resultado_ciclo_cpu;

t_resultado_ciclo_cpu ejecutar_ciclo_proceso(
    // KS se usa para syscalls/interrupciones y KM para cada fetch.
    int socket_kernel,
    int socket_memory,
    // Los accesos físicos resuelven el MS por ID cuando se usa por primera vez.
    t_cpu_memory_sticks* memory_sticks,
    uint32_t pid,
    t_contexto* contexto,
    t_registros_cpu* registros,
    t_log* logger
);

#endif
