#ifndef CPU_DEVOLUCION_H_
#define CPU_DEVOLUCION_H_

#include <stdbool.h>
#include <stdint.h>
#include <commons/log.h>
#include <utils/protocolo.h>

#include "cpu_registros.h"

// El contexto completo va a KM. A KS le devuelvo este resumen con PID, motivo y PC.
bool devolver_proceso_a_scheduler(
    int socket_kernel,
    uint32_t pid,
    t_motivo_devolucion_cpu motivo,
    t_registros_cpu* registros,
    t_log* logger
);

const char* motivo_devolucion_to_string(t_motivo_devolucion_cpu motivo);

#endif
