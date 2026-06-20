#ifndef CPU_MMU_H_
#define CPU_MMU_H_

<<<<<<< HEAD
#include <stdint.h>
#include <utils/sockets.h>

typedef enum {
    CPU_MMU_OK,
    CPU_MMU_SEGMENTO_NO_ENCONTRADO,
    CPU_MMU_SEGMENTATION_FAULT,
    CPU_MMU_ERROR
} t_resultado_mmu;

typedef struct {
    uint32_t id_segmento;
    uint32_t desplazamiento;
    uint32_t id_memory_stick;
    uint32_t direccion_fisica;
    uint32_t limite_segmento;
} t_traduccion_mmu;

t_resultado_mmu traducir_direccion_logica(
    t_contexto* contexto,
    uint32_t direccion_logica,
    uint32_t tamanio,
    uint32_t tamanio_max_segmento,
    t_traduccion_mmu* traduccion
);

const char* resultado_mmu_to_string(t_resultado_mmu resultado);

#endif
=======
#include <stdbool.h>
#include <stdint.h>
#include <utils/sockets.h>

typedef struct {
    uint32_t id_memory_stick;
    uint32_t direccion_fisica;
} t_traduccion;

bool mmu_traducir(t_contexto* contexto, uint32_t direccion_logica, uint32_t tamanio_operacion, t_traduccion* resultado);

#endif
>>>>>>> feature/cpu/mmu
