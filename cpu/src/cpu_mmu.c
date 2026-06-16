#include "cpu_mmu.h"

#include <stddef.h>

static t_segmento* buscar_segmento(t_contexto* contexto, uint32_t id_segmento) {
    if (contexto == NULL || contexto->segmentos == NULL) {
        return NULL;
    }

    for (uint32_t i = 0; i < contexto->cant_segmentos; i++) {
        if (contexto->segmentos[i].id_segmento == id_segmento) {
            return &contexto->segmentos[i];
        }
    }

    return NULL;
}

t_resultado_mmu traducir_direccion_logica(
    t_contexto* contexto,
    uint32_t direccion_logica,
    uint32_t tamanio,
    uint32_t tamanio_max_segmento,
    t_traduccion_mmu* traduccion
) {
    if (contexto == NULL || traduccion == NULL || tamanio == 0 || tamanio_max_segmento == 0) {
        return CPU_MMU_ERROR;
    }

    uint32_t id_segmento = direccion_logica / tamanio_max_segmento;
    uint32_t desplazamiento = direccion_logica % tamanio_max_segmento;
    t_segmento* segmento = buscar_segmento(contexto, id_segmento);
    if (segmento == NULL) {
        return CPU_MMU_SEGMENTO_NO_ENCONTRADO;
    }

    if (desplazamiento > UINT32_MAX - tamanio || desplazamiento + tamanio > segmento->limite) {
        return CPU_MMU_SEGMENTATION_FAULT;
    }

    if (segmento->base_fisica > UINT32_MAX - desplazamiento) {
        return CPU_MMU_ERROR;
    }

    traduccion->id_segmento = id_segmento;
    traduccion->desplazamiento = desplazamiento;
    traduccion->id_memory_stick = segmento->id_memory_stick;
    traduccion->direccion_fisica = segmento->base_fisica + desplazamiento;
    traduccion->limite_segmento = segmento->limite;

    return CPU_MMU_OK;
}

const char* resultado_mmu_to_string(t_resultado_mmu resultado) {
    switch (resultado) {
        case CPU_MMU_OK:
            return "OK";
        case CPU_MMU_SEGMENTO_NO_ENCONTRADO:
            return "SEGMENTO_NO_ENCONTRADO";
        case CPU_MMU_SEGMENTATION_FAULT:
            return "SEGMENTATION_FAULT";
        case CPU_MMU_ERROR:
            return "ERROR";
        default:
            return "DESCONOCIDO";
    }
}
