#include <cspecs/cspec.h>
#include <string.h>
#include "ks_cmn.h"

context(ks_cmn) {

    describe("parsear_queues_algorithms — cantidad de colas") {

        it("retorna 1 con una sola cola") {
            char out[MAX_COLAS][8];
            should_int(parsear_queues_algorithms("[FIFO]", out)) be equal to(1);
        } end

        it("retorna 2 con dos colas") {
            char out[MAX_COLAS][8];
            should_int(parsear_queues_algorithms("[FIFO,RR]", out)) be equal to(2);
        } end

        it("retorna 4 con cuatro colas") {
            char out[MAX_COLAS][8];
            should_int(parsear_queues_algorithms("[FIFO,RR,RR,FIFO]", out)) be equal to(4);
        } end

        it("retorna MAX_COLAS si la cadena tiene más entradas") {
            // Construir "[RR,RR,...,RR]" con MAX_COLAS + 2 entradas
            char big[512] = "[";
            for (int i = 0; i < MAX_COLAS + 2; i++) {
                strcat(big, "RR");
                if (i < MAX_COLAS + 1) strcat(big, ",");
            }
            strcat(big, "]");
            char out[MAX_COLAS][8];
            should_int(parsear_queues_algorithms(big, out)) be equal to(MAX_COLAS);
        } end

    } end

    describe("parsear_queues_algorithms — algoritmo por cola") {

        it("una cola FIFO queda como FIFO") {
            char out[MAX_COLAS][8];
            parsear_queues_algorithms("[FIFO]", out);
            should_string(out[0]) be equal to("FIFO");
        } end

        it("una cola RR queda como RR") {
            char out[MAX_COLAS][8];
            parsear_queues_algorithms("[RR]", out);
            should_string(out[0]) be equal to("RR");
        } end

        it("carga correctamente cuatro colas mixtas") {
            char out[MAX_COLAS][8];
            parsear_queues_algorithms("[FIFO,RR,RR,FIFO]", out);
            should_string(out[0]) be equal to("FIFO");
            should_string(out[1]) be equal to("RR");
            should_string(out[2]) be equal to("RR");
            should_string(out[3]) be equal to("FIFO");
        } end

        it("seis colas — ejemplo del config de la cátedra") {
            char out[MAX_COLAS][8];
            int n = parsear_queues_algorithms("[FIFO,RR,RR,FIFO,RR,FIFO]", out);
            should_int(n) be equal to(6);
            should_string(out[0]) be equal to("FIFO");
            should_string(out[1]) be equal to("RR");
            should_string(out[2]) be equal to("RR");
            should_string(out[3]) be equal to("FIFO");
            should_string(out[4]) be equal to("RR");
            should_string(out[5]) be equal to("FIFO");
        } end

        it("ignora espacios al inicio de cada token") {
            char out[MAX_COLAS][8];
            parsear_queues_algorithms("[FIFO, RR, FIFO]", out);
            should_string(out[0]) be equal to("FIFO");
            should_string(out[1]) be equal to("RR");
            should_string(out[2]) be equal to("FIFO");
        } end

    } end

    describe("parsear_queues_algorithms — casos límite") {

        it("cadena con un solo elemento sin corchete de cierre") {
            char out[MAX_COLAS][8];
            int n = parsear_queues_algorithms("[RR", out);
            should_int(n) be equal to(1);
            should_string(out[0]) be equal to("RR");
        } end

        it("no modifica entradas más allá de n colas") {
            char out[MAX_COLAS][8];
            // Inicializar todo con "XX" para verificar que no se toca más allá de n
            for (int i = 0; i < MAX_COLAS; i++) strncpy(out[i], "XX", 8);
            int n = parsear_queues_algorithms("[FIFO,RR]", out);
            should_int(n) be equal to(2);
            should_string(out[2]) be equal to("XX");
        } end

    } end

}
