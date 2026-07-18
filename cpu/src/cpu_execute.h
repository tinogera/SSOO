#ifndef CPU_EXECUTE_H_
#define CPU_EXECUTE_H_

#include <stdint.h>
#include <commons/log.h>

#include "cpu_decode.h"
#include "cpu_registros.h"
#include <utils/sockets.h>

// Resultado de ejecutar una instrucción básica. Las instrucciones de memoria
// tienen su propio enum porque necesitan distinguir el segmentation fault.
typedef enum {
    CPU_EXEC_OK,
    CPU_EXEC_ERROR,
    CPU_EXEC_SEG_FAULT // Reservado; las básicas actuales no acceden a memoria.
} t_resultado_ejecucion;

t_resultado_ejecucion ejecutar_instruccion(
    t_instruccion_decodificada* instruccion,
    t_registros_cpu* registros,
    uint32_t pid,
    t_log* logger
);

#endif
