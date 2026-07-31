#include "cpu_mmu.h"

#include <stddef.h>

/*
 * La MMU traduce una dirección lógica a: segmento, desplazamiento, MS y
 * dirección física. No lee memoria; solamente valida y calcula la traducción.
 */

// Queda global porque todas las traducciones de esta CPU usan el mismo valor.
static uint32_t g_segment_max_size = 256;

void set_segment_max_size(uint32_t tamanio) {
    g_segment_max_size = tamanio;
}
// Busco por id y no por posición, porque la tabla puede no estar ordenada o
// puede haber perdido entradas después de un MEM_FREE.
static t_entrada_segmento* buscar_segmento(t_contexto* contexto, uint32_t id_segmento) {
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
    t_traduccion_mmu* traduccion
) {
    uint32_t tamanio_max_segmento = g_segment_max_size;
    
    if (contexto == NULL || traduccion == NULL || tamanio == 0 || tamanio_max_segmento == 0) {
        return CPU_MMU_ERROR;
    }

    // Esta es la cuenta clave de segmentación: cociente = segmento,
    // resto = desplazamiento dentro del segmento.
    uint32_t id_segmento = direccion_logica / tamanio_max_segmento;
    uint32_t desplazamiento = direccion_logica % tamanio_max_segmento;
    t_entrada_segmento* segmento = buscar_segmento(contexto, id_segmento);
    if (segmento == NULL) {
        return CPU_MMU_SEGMENTO_NO_ENCONTRADO;
    }

    // Valido el acceso completo, no sólo el primer byte. El primer término
    // evita que la suma haga overflow antes de compararla con el límite.
    if (desplazamiento > UINT32_MAX - tamanio || desplazamiento + tamanio > segmento->limite) {
        return CPU_MMU_SEGMENTATION_FAULT;
    }

    // Mismo cuidado para base + desplazamiento.
    if (segmento->base > UINT32_MAX - desplazamiento) {
        return CPU_MMU_ERROR;
    }

    // Devuelvo todo junto para que memoria sepa tanto la dirección como el fd
    // del Memory Stick que tiene que usar.
    traduccion->id_segmento = id_segmento;
    traduccion->desplazamiento = desplazamiento;
    traduccion->id_memory_stick = segmento->id_memory_stick;
    traduccion->direccion_fisica = segmento->base + desplazamiento;
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

