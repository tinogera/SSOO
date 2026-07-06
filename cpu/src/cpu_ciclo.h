#ifndef CPU_CICLO_H_
#define CPU_CICLO_H_

#include <stdint.h>
#include <commons/log.h>

#include "cpu_registros.h"

typedef enum {
    CPU_CICLO_OK,
    CPU_CICLO_ERROR_FETCH,
    CPU_CICLO_ERROR_DECODE,
    CPU_CICLO_ERROR_EXECUTE,
    CPU_CICLO_SYSCALL,
    CPU_CICLO_EXIT,
    CPU_CICLO_INTERRUPCION,
    CPU_CICLO_SEG_FAULT
} t_resultado_ciclo_cpu;

t_resultado_ciclo_cpu ejecutar_ciclo_proceso(
    int socket_kernel,
    int socket_memory,
    int* sockets_ms,
    int n_sockets_ms,
    uint32_t pid,
    t_contexto* contexto,
    t_registros_cpu* registros,
    t_log* logger
);

#endif
