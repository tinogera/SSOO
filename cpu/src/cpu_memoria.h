#ifndef CPU_MEMORIA_H_
#define CPU_MEMORIA_H_

#include <stdint.h>
#include <commons/log.h>
#include <utils/sockets.h>

#include "cpu_decode.h"
#include "cpu_registros.h"

typedef enum {
    CPU_MEMORIA_OK,
    CPU_MEMORIA_ERROR,
    CPU_MEMORIA_SEG_FAULT
} t_resultado_memoria_cpu;

t_resultado_memoria_cpu ejecutar_instruccion_memoria(
    int socket_memoria,
    t_instruccion_decodificada* instruccion,
    t_contexto* contexto,
    t_registros_cpu* registros,
    uint32_t pid,
    uint32_t tamanio_max_segmento,
    t_log* logger
);

#endif
