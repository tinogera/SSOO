#ifndef CPU_MMU_H_
#define CPU_MMU_H_

#include <stdint.h>
#include <utils/sockets.h>

typedef enum {
    CPU_MMU_OK,
    CPU_MMU_SEGMENTO_NO_ENCONTRADO,
    CPU_MMU_SEGMENTATION_FAULT,
    CPU_MMU_ERROR
} t_resultado_mmu;

typedef struct {
    // Además de la dirección física guardo cómo llegué a ella y en qué MS está.
    uint32_t id_segmento;
    uint32_t desplazamiento;
    uint32_t id_memory_stick;
    uint32_t direccion_fisica;
    uint32_t limite_segmento;
} t_traduccion_mmu;

// main carga este valor desde config antes de ejecutar procesos.
void set_segment_max_size(uint32_t tamanio);

t_resultado_mmu traducir_direccion_logica(
    t_contexto* contexto,
    uint32_t direccion_logica,
    uint32_t tamanio,
    t_traduccion_mmu* traduccion
);

const char* resultado_mmu_to_string(t_resultado_mmu resultado);

#endif
