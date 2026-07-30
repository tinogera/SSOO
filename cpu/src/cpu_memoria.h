#ifndef CPU_MEMORIA_H_
#define CPU_MEMORIA_H_

#include <stdbool.h>
#include <stdint.h>
#include <commons/log.h>
#include <utils/sockets.h>

#include "cpu_decode.h"
#include "cpu_memory_sticks.h"
#include "cpu_registros.h"

// Distingo un fallo técnico de un acceso lógico inválido, porque KS necesita
// recibir SEG_FAULT como motivo específico.
typedef enum {
    CPU_MEMORIA_OK,
    CPU_MEMORIA_ERROR,
    CPU_MEMORIA_SEG_FAULT
} t_resultado_memoria_cpu;

// Estos helpers reciben el fd del MS elegido aunque el nombre histórico diga memory.
bool memoria_read(int socket_memory, uint32_t direccion, uint32_t tamanio, void* valor);
bool memoria_write(int socket_memory, uint32_t direccion, uint32_t tamanio, const void* datos);

t_resultado_memoria_cpu ejecutar_instruccion_memoria(
    t_cpu_memory_sticks* memory_sticks,
    t_instruccion_decodificada* instruccion,
    t_contexto* contexto,
    t_registros_cpu* registros,
    uint32_t pid,
    t_log* logger
);

#endif
