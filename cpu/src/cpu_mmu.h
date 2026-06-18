#ifndef CPU_MMU_H_
#define CPU_MMU_H_

#include <stdbool.h>
#include <stdint.h>
#include <utils/sockets.h>

typedef struct {
    uint32_t id_memory_stick;
    uint32_t direccion_fisica;
} t_traduccion;

bool mmu_traducir(t_contexto* contexto, uint32_t direccion_logica, uint32_t tamanio_operacion, t_traduccion* resultado);

#endif