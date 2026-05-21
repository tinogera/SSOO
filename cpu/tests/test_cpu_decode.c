#include <cspecs/cspec.h>
#include <string.h>

#include "../src/cpu_decode.h"

context (cpu_decode) {

    describe ("instrucciones basicas") {

        it ("decodifica NOOP sin parametros") {
            t_instruccion_decodificada instruccion = decode_instruccion("NOOP");

            should_int(instruccion.opcode) be equal to(CPU_INST_NOOP);
            should_int(instruccion.cantidad_parametros) be equal to(0);
        } end

        it ("decodifica SET con registro y valor") {
            t_instruccion_decodificada instruccion = decode_instruccion("SET AX 5");

            should_int(instruccion.opcode) be equal to(CPU_INST_SET);
            should_int(instruccion.cantidad_parametros) be equal to(2);
            should_string(instruccion.parametros[0]) be equal to("AX");
            should_string(instruccion.parametros[1]) be equal to("5");
        } end

        it ("decodifica JNZ con registro e instruccion destino") {
            t_instruccion_decodificada instruccion = decode_instruccion("JNZ AX 4");

            should_int(instruccion.opcode) be equal to(CPU_INST_JNZ);
            should_int(instruccion.cantidad_parametros) be equal to(2);
            should_string(instruccion.parametros[0]) be equal to("AX");
            should_string(instruccion.parametros[1]) be equal to("4");
        } end

    } end

    describe ("syscalls") {

        it ("decodifica MUTEX_LOCK") {
            t_instruccion_decodificada instruccion = decode_instruccion("MUTEX_LOCK MUTEX_1");

            should_int(instruccion.opcode) be equal to(CPU_INST_MUTEX_LOCK);
            should_int(instruccion.cantidad_parametros) be equal to(1);
            should_string(instruccion.parametros[0]) be equal to("MUTEX_1");
        } end

        it ("decodifica STDOUT con registros direccion y tamanio") {
            t_instruccion_decodificada instruccion = decode_instruccion("STDOUT SI AX");

            should_int(instruccion.opcode) be equal to(CPU_INST_STDOUT);
            should_int(instruccion.cantidad_parametros) be equal to(2);
            should_string(instruccion.parametros[0]) be equal to("SI");
            should_string(instruccion.parametros[1]) be equal to("AX");
        } end

        it ("decodifica EXIT sin parametros") {
            t_instruccion_decodificada instruccion = decode_instruccion("EXIT");

            should_int(instruccion.opcode) be equal to(CPU_INST_EXIT);
            should_int(instruccion.cantidad_parametros) be equal to(0);
        } end

    } end

    describe ("casos invalidos") {

        it ("devuelve UNKNOWN para instruccion desconocida") {
            t_instruccion_decodificada instruccion = decode_instruccion("FOO AX");

            should_int(instruccion.opcode) be equal to(CPU_INST_UNKNOWN);
        } end

        it ("tolera strings nulos") {
            t_instruccion_decodificada instruccion = decode_instruccion(NULL);

            should_int(instruccion.opcode) be equal to(CPU_INST_UNKNOWN);
            should_int(instruccion.cantidad_parametros) be equal to(0);
        } end

    } end

}
