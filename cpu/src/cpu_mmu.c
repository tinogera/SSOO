#include "cpu_mmu.h"

static t_entrada_segmento* buscar_segmento(t_contexto* contexto, uint32_t id_segmento) {

    for(uint32_t i = 0; i < contexto->cant_segmentos; i++) {

        if(contexto->segmentos[i].id_segmento == id_segmento) {
            return &contexto->segmentos[i];
        }
    }

    return NULL;
}

bool mmu_traducir(t_contexto* contexto, uint32_t direccion_logica, uint32_t tamanio_operacion, t_traduccion* resultado) {

    uint32_t numero_segmento = floor(direccion_logica / contexto->segment_max_size);
    uint32_t desplazamiento = direccion_logica % contexto->segment_max_size;

    t_entrada_segmento* segmento = buscar_segmento(contexto, numero_segmento);

    if(segmento == NULL) {
        return false;
    }

    if(desplazamiento + tamanio_operacion > segmento->limite) {
        return false;
    }

    resultado->id_memory_stick = segmento->id_memory_stick;
    resultado->direccion_fisica = segmento->base + desplazamiento;

    return true;
}