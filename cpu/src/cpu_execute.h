#ifndef CPU_EXECUTE_H_
#define CPU_EXECUTE_H_

#include <stdint.h>
#include <commons/log.h>

#include "cpu_decode.h"
#include "cpu_registros.h"

typedef enum {
    CPU_EXEC_OK,
    CPU_EXEC_ERROR
} t_resultado_ejecucion;

t_resultado_ejecucion ejecutar_instruccion(
    t_instruccion_decodificada* instruccion,
    t_registros_cpu* registros,
    uint32_t pid,
    t_log* logger
);

#endif
