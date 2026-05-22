#include <cspecs/cspec.h>
#include <commons/log.h>

#include "../src/cpu_decode.h"
#include "../src/cpu_execute.h"
#include "../src/cpu_registros.h"

static t_log* crear_logger_test(void) {
    return log_create("/tmp/cpu_execute_test.log", "cpu_execute_test", false, LOG_LEVEL_ERROR);
}

context (cpu_execute) {

    describe ("NOOP") {

        it ("incrementa el program counter") {
            t_log* logger = crear_logger_test();
            t_registros_cpu registros;
            inicializar_registros_cpu(&registros);

            t_instruccion_decodificada instruccion = decode_instruccion("NOOP");
            t_resultado_ejecucion resultado = ejecutar_instruccion(&instruccion, &registros, 1, logger);

            should_int(resultado) be equal to(CPU_EXEC_OK);
            should_int(registros.pc) be equal to(1);

            log_destroy(logger);
        } end

    } end

    describe ("SET") {

        it ("asigna un valor a un registro de 8 bits") {
            t_log* logger = crear_logger_test();
            t_registros_cpu registros;
            inicializar_registros_cpu(&registros);

            t_instruccion_decodificada instruccion = decode_instruccion("SET AX 7");
            t_resultado_ejecucion resultado = ejecutar_instruccion(&instruccion, &registros, 1, logger);

            should_int(resultado) be equal to(CPU_EXEC_OK);
            should_int(registros.ax) be equal to(7);
            should_int(registros.pc) be equal to(1);

            log_destroy(logger);
        } end

        it ("asigna un valor a un registro de 32 bits") {
            t_log* logger = crear_logger_test();
            t_registros_cpu registros;
            inicializar_registros_cpu(&registros);

            t_instruccion_decodificada instruccion = decode_instruccion("SET EAX 1024");
            t_resultado_ejecucion resultado = ejecutar_instruccion(&instruccion, &registros, 1, logger);

            should_int(resultado) be equal to(CPU_EXEC_OK);
            should_int(registros.eax) be equal to(1024);
            should_int(registros.pc) be equal to(1);

            log_destroy(logger);
        } end

    } end

    describe ("SUM") {

        it ("suma el registro origen al destino") {
            t_log* logger = crear_logger_test();
            t_registros_cpu registros;
            inicializar_registros_cpu(&registros);
            registros.ax = 5;
            registros.bx = 3;

            t_instruccion_decodificada instruccion = decode_instruccion("SUM AX BX");
            t_resultado_ejecucion resultado = ejecutar_instruccion(&instruccion, &registros, 1, logger);

            should_int(resultado) be equal to(CPU_EXEC_OK);
            should_int(registros.ax) be equal to(8);
            should_int(registros.pc) be equal to(1);

            log_destroy(logger);
        } end

    } end

    describe ("SUB") {

        it ("resta el registro origen al destino") {
            t_log* logger = crear_logger_test();
            t_registros_cpu registros;
            inicializar_registros_cpu(&registros);
            registros.eax = 10;
            registros.ebx = 4;

            t_instruccion_decodificada instruccion = decode_instruccion("SUB EAX EBX");
            t_resultado_ejecucion resultado = ejecutar_instruccion(&instruccion, &registros, 1, logger);

            should_int(resultado) be equal to(CPU_EXEC_OK);
            should_int(registros.eax) be equal to(6);
            should_int(registros.pc) be equal to(1);

            log_destroy(logger);
        } end

    } end

    describe ("JNZ") {

        it ("salta a la instruccion indicada si el registro no es cero") {
            t_log* logger = crear_logger_test();
            t_registros_cpu registros;
            inicializar_registros_cpu(&registros);
            registros.ax = 1;

            t_instruccion_decodificada instruccion = decode_instruccion("JNZ AX 4");
            t_resultado_ejecucion resultado = ejecutar_instruccion(&instruccion, &registros, 1, logger);

            should_int(resultado) be equal to(CPU_EXEC_OK);
            should_int(registros.pc) be equal to(4);

            log_destroy(logger);
        } end

        it ("incrementa el program counter si el registro es cero") {
            t_log* logger = crear_logger_test();
            t_registros_cpu registros;
            inicializar_registros_cpu(&registros);
            registros.ax = 0;

            t_instruccion_decodificada instruccion = decode_instruccion("JNZ AX 4");
            t_resultado_ejecucion resultado = ejecutar_instruccion(&instruccion, &registros, 1, logger);

            should_int(resultado) be equal to(CPU_EXEC_OK);
            should_int(registros.pc) be equal to(1);

            log_destroy(logger);
        } end

    } end

}
