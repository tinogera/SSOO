#include <arpa/inet.h>
#include <commons/log.h>
#include <cspecs/cspec.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/socket.h>
#include <unistd.h>
#include <utils/protocolo.h>
#include <utils/sockets.h>

#include "../src/cpu_interrupciones.h"

static t_log* crear_logger_interrupciones_test(void) {
    return log_create(
        "/tmp/cpu_interrupciones_test.log",
        "cpu_interrupciones_test",
        false,
        LOG_LEVEL_ERROR
    );
}

static void crear_socketpair_test(int sockets[2]) {
    should_int(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets)) be equal to(0);
}

static void cerrar_socketpair_test(int sockets[2]) {
    close(sockets[0]);
    close(sockets[1]);
}

static void enviar_interrupcion_test(int socket, uint32_t pid, t_motivo_interrupcion_cpu motivo) {
    t_payload_interrupcion_cpu payload = {
        .pid = htonl(pid),
        .motivo = htonl((uint32_t) motivo)
    };

    enviar_mensaje(socket, MSG_INTERRUPCION_CPU, &payload, sizeof(payload));
}

context (cpu_interrupciones) {

    describe ("recibir_interrupcion_cpu_si_hay") {

        it ("recibe la interrupcion del PID actual y convierte el payload a host order") {
            int sockets[2];
            crear_socketpair_test(sockets);
            t_log* logger = crear_logger_interrupciones_test();
            t_interrupcion_cpu interrupcion = {0};

            enviar_interrupcion_test(sockets[0], 0x01020304, MOTIVO_INTERRUPCION_DESALOJO);

            t_resultado_interrupcion_cpu resultado = recibir_interrupcion_cpu_si_hay(
                sockets[1],
                0x01020304,
                &interrupcion,
                logger
            );

            should_int(resultado) be equal to(CPU_INTERRUPCION_RECIBIDA);
            should_int(interrupcion.pid) be equal to(0x01020304);
            should_int(interrupcion.motivo) be equal to(MOTIVO_INTERRUPCION_DESALOJO);

            cerrar_socketpair_test(sockets);
            log_destroy(logger);
        } end

        it ("descarta una interrupcion atrasada de otro PID") {
            int sockets[2];
            crear_socketpair_test(sockets);
            t_log* logger = crear_logger_interrupciones_test();
            t_interrupcion_cpu interrupcion = {
                .pid = 77,
                .motivo = MOTIVO_INTERRUPCION_QUANTUM
            };

            enviar_interrupcion_test(sockets[0], 8, MOTIVO_INTERRUPCION_DESALOJO);

            t_resultado_interrupcion_cpu resultado = recibir_interrupcion_cpu_si_hay(
                sockets[1],
                9,
                &interrupcion,
                logger
            );

            should_int(resultado) be equal to(CPU_INTERRUPCION_SIN_MENSAJE);
            should_int(interrupcion.pid) be equal to(77);
            should_int(interrupcion.motivo) be equal to(MOTIVO_INTERRUPCION_QUANTUM);

            cerrar_socketpair_test(sockets);
            log_destroy(logger);
        } end

        it ("rechaza un opcode inesperado") {
            int sockets[2];
            crear_socketpair_test(sockets);
            t_log* logger = crear_logger_interrupciones_test();
            t_interrupcion_cpu interrupcion = {0};

            enviar_mensaje(sockets[0], MSG_OK, NULL, 0);

            t_resultado_interrupcion_cpu resultado = recibir_interrupcion_cpu_si_hay(
                sockets[1],
                5,
                &interrupcion,
                logger
            );

            should_int(resultado) be equal to(CPU_INTERRUPCION_ERROR);

            cerrar_socketpair_test(sockets);
            log_destroy(logger);
        } end

        it ("rechaza un payload de interrupcion con tamanio invalido") {
            int sockets[2];
            crear_socketpair_test(sockets);
            t_log* logger = crear_logger_interrupciones_test();
            t_interrupcion_cpu interrupcion = {0};
            uint32_t payload_incompleto = htonl(5);

            enviar_mensaje(
                sockets[0],
                MSG_INTERRUPCION_CPU,
                &payload_incompleto,
                sizeof(payload_incompleto)
            );

            t_resultado_interrupcion_cpu resultado = recibir_interrupcion_cpu_si_hay(
                sockets[1],
                5,
                &interrupcion,
                logger
            );

            should_int(resultado) be equal to(CPU_INTERRUPCION_ERROR);

            cerrar_socketpair_test(sockets);
            log_destroy(logger);
        } end

        it ("informa error cuando Kernel Scheduler cierra el socket") {
            int sockets[2];
            crear_socketpair_test(sockets);
            t_log* logger = crear_logger_interrupciones_test();
            t_interrupcion_cpu interrupcion = {0};

            close(sockets[0]);

            t_resultado_interrupcion_cpu resultado = recibir_interrupcion_cpu_si_hay(
                sockets[1],
                5,
                &interrupcion,
                logger
            );

            should_int(resultado) be equal to(CPU_INTERRUPCION_ERROR);

            close(sockets[1]);
            log_destroy(logger);
        } end

    } end

}
