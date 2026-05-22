#include <cspecs/cspec.h>

#include "../src/cpu_devolucion.h"
#include "../src/cpu_registros.h"

context (cpu_registros) {

    describe ("inicializar_registros_cpu") {

        it ("inicializa todos los registros en cero") {
            t_registros_cpu registros;
            inicializar_registros_cpu(&registros);

            should_int(registros.pc) be equal to(0);
            should_int(registros.ax) be equal to(0);
            should_int(registros.bx) be equal to(0);
            should_int(registros.cx) be equal to(0);
            should_int(registros.dx) be equal to(0);
            should_int(registros.eax) be equal to(0);
            should_int(registros.ebx) be equal to(0);
            should_int(registros.ecx) be equal to(0);
            should_int(registros.edx) be equal to(0);
            should_int(registros.si) be equal to(0);
            should_int(registros.di) be equal to(0);
        } end

    } end

    describe ("leer_valor_registro_cpu") {

        it ("lee registros de 8 bits") {
            t_registros_cpu registros;
            inicializar_registros_cpu(&registros);
            registros.ax = 9;

            uint32_t valor = 0;
            should_bool(leer_valor_registro_cpu(&registros, "AX", &valor)) be truthy;
            should_int(valor) be equal to(9);
        } end

        it ("lee registros de 32 bits") {
            t_registros_cpu registros;
            inicializar_registros_cpu(&registros);
            registros.eax = 1024;

            uint32_t valor = 0;
            should_bool(leer_valor_registro_cpu(&registros, "EAX", &valor)) be truthy;
            should_int(valor) be equal to(1024);
        } end

        it ("rechaza registros desconocidos") {
            t_registros_cpu registros;
            inicializar_registros_cpu(&registros);

            uint32_t valor = 0;
            should_bool(leer_valor_registro_cpu(&registros, "FOO", &valor)) not be truthy;
        } end

    } end

}

context (cpu_devolucion) {

    describe ("motivo_devolucion_to_string") {

        it ("mapea motivos conocidos") {
            should_string(motivo_devolucion_to_string(MOTIVO_DEVOLUCION_SYSCALL)) be equal to("SYSCALL");
            should_string(motivo_devolucion_to_string(MOTIVO_DEVOLUCION_EXIT)) be equal to("EXIT");
            should_string(motivo_devolucion_to_string(MOTIVO_DEVOLUCION_ERROR)) be equal to("ERROR");
            should_string(motivo_devolucion_to_string(MOTIVO_DEVOLUCION_INTERRUPCION)) be equal to("INTERRUPCION");
        } end

    } end

}
