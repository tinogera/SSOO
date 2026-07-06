#include <cspecs/cspec.h>

#include "../src/cpu_mmu.h"

context (cpu_mmu) {

    describe ("traducir_direccion_logica") {

        it ("traduce una direccion logica a direccion fisica") {
            t_entrada_segmento segmentos[] = {
                {
                    .id_segmento = 2,
                    .id_memory_stick = 5,
                    .base = 1000,
                    .limite = 64
                }
            };
            t_contexto contexto = {
                .pid = 1,
                .cant_segmentos = 1,
                .segmentos = segmentos
            };
            t_traduccion_mmu traduccion;

            set_segment_max_size(256);
            t_resultado_mmu resultado = traducir_direccion_logica(
                &contexto,
                524,
                4,
                &traduccion
            );

            should_int(resultado) be equal to(CPU_MMU_OK);
            should_int(traduccion.id_segmento) be equal to(2);
            should_int(traduccion.desplazamiento) be equal to(12);
            should_int(traduccion.id_memory_stick) be equal to(5);
            should_int(traduccion.direccion_fisica) be equal to(1012);
        } end

        it ("detecta segmento inexistente") {
            t_entrada_segmento segmentos[] = {
                {
                    .id_segmento = 1,
                    .id_memory_stick = 3,
                    .base = 500,
                    .limite = 32
                }
            };
            t_contexto contexto = {
                .pid = 1,
                .cant_segmentos = 1,
                .segmentos = segmentos
            };
            t_traduccion_mmu traduccion;

            set_segment_max_size(256);
            t_resultado_mmu resultado = traducir_direccion_logica(
                &contexto,
                524,
                4,
                &traduccion
            );

            should_int(resultado) be equal to(CPU_MMU_SEGMENTO_NO_ENCONTRADO);
        } end

        it ("detecta segmentation fault si supera el limite") {
            t_entrada_segmento segmentos[] = {
                {
                    .id_segmento = 2,
                    .id_memory_stick = 5,
                    .base = 1000,
                    .limite = 16
                }
            };
            t_contexto contexto = {
                .pid = 1,
                .cant_segmentos = 1,
                .segmentos = segmentos
            };
            t_traduccion_mmu traduccion;

            set_segment_max_size(256);
            t_resultado_mmu resultado = traducir_direccion_logica(
                &contexto,
                524,
                8,
                &traduccion
            );

            should_int(resultado) be equal to(CPU_MMU_SEGMENTATION_FAULT);
        } end

    } end

    describe ("resultado_mmu_to_string") {

        it ("mapea resultados conocidos") {
            should_string(resultado_mmu_to_string(CPU_MMU_OK)) be equal to("OK");
            should_string(resultado_mmu_to_string(CPU_MMU_SEGMENTATION_FAULT)) be equal to("SEGMENTATION_FAULT");
        } end

    } end

}
