#include <cspecs/cspec.h>
#include <sys/socket.h>
#include <time.h>
#include <stdlib.h>
#include <unistd.h>
#include "io_utils.h"

// Crea un t_mensaje de MSG_IO_SLEEP listo para pasar a manejar_sleep.
// manejar_sleep llama free_mensaje internamente, así que no liberar después.
static t_mensaje* crear_msg_sleep(uint32_t pid, uint32_t tiempo_ms) {
    t_mensaje* msg        = malloc(sizeof(t_mensaje));
    t_payload_io_sleep* p = malloc(sizeof(t_payload_io_sleep));
    p->pid       = pid;
    p->tiempo_ms = tiempo_ms;
    msg->op_code      = MSG_IO_SLEEP;
    msg->payload_size = sizeof(t_payload_io_sleep);
    msg->payload      = p;
    return msg;
}

context (io_utils) {

    describe ("es_tipo_valido") {

        it ("acepta STDIN") {
            should_bool(es_tipo_valido("STDIN")) be truthy;
        } end

        it ("acepta STDOUT") {
            should_bool(es_tipo_valido("STDOUT")) be truthy;
        } end

        it ("acepta SLEEP") {
            should_bool(es_tipo_valido("SLEEP")) be truthy;
        } end

        it ("rechaza un tipo desconocido") {
            should_bool(es_tipo_valido("FOO")) not be truthy;
        } end

        it ("rechaza string vacío") {
            should_bool(es_tipo_valido("")) not be truthy;
        } end

        it ("distingue mayúsculas y minúsculas") {
            should_bool(es_tipo_valido("sleep")) not be truthy;
            should_bool(es_tipo_valido("Sleep")) not be truthy;
        } end

    } end

    describe ("manejar_sleep") {

        // socketpair crea dos file descriptors conectados entre sí (como un pipe
        // bidireccional). fds[0] simula el lado del KS y fds[1] el lado de IO.
        // Así podemos llamar a manejar_sleep(msg, fds[1], ...) y leer el
        // MSG_IO_FIN desde fds[0] sin necesidad de levantar un servidor real.

        it ("envía MSG_IO_FIN con el PID correcto al terminar") {
            int fds[2];
            socketpair(AF_UNIX, SOCK_STREAM, 0, fds);
            t_log* log = log_create("/dev/null", "test", false, LOG_LEVEL_ERROR);

            manejar_sleep(crear_msg_sleep(99, 1), fds[1], log);

            t_mensaje* fin = recibir_mensaje(fds[0]);
            should_ptr(fin) not be null;
            should_int(fin->op_code) be equal to(MSG_IO_FIN);

            t_payload_io_fin* payload = (t_payload_io_fin*) fin->payload;
            should_int(payload->pid) be equal to(99);

            free_mensaje(fin);
            log_destroy(log);
            close(fds[0]); close(fds[1]);
        } end

        it ("no confunde el PID cuando se llama dos veces seguidas") {
            int fds[2];
            socketpair(AF_UNIX, SOCK_STREAM, 0, fds);
            t_log* log = log_create("/dev/null", "test", false, LOG_LEVEL_ERROR);

            manejar_sleep(crear_msg_sleep(1, 1), fds[1], log);
            manejar_sleep(crear_msg_sleep(2, 1), fds[1], log);

            t_mensaje* fin1 = recibir_mensaje(fds[0]);
            t_mensaje* fin2 = recibir_mensaje(fds[0]);

            should_int(((t_payload_io_fin*)fin1->payload)->pid) be equal to(1);
            should_int(((t_payload_io_fin*)fin2->payload)->pid) be equal to(2);

            free_mensaje(fin1); free_mensaje(fin2);
            log_destroy(log);
            close(fds[0]); close(fds[1]);
        } end

        it ("duerme al menos el tiempo solicitado") {
            int fds[2];
            socketpair(AF_UNIX, SOCK_STREAM, 0, fds);
            t_log* log = log_create("/dev/null", "test", false, LOG_LEVEL_ERROR);

            struct timespec inicio, fin_ts;
            clock_gettime(CLOCK_MONOTONIC, &inicio);

            manejar_sleep(crear_msg_sleep(1, 100), fds[1], log);

            clock_gettime(CLOCK_MONOTONIC, &fin_ts);
            long ms = (fin_ts.tv_sec  - inicio.tv_sec)  * 1000
                    + (fin_ts.tv_nsec - inicio.tv_nsec) / 1000000;

            // Margen del 10%: en CI los schedulers pueden tardar un poco más
            should_bool(ms >= 90) be truthy;

            t_mensaje* fin = recibir_mensaje(fds[0]);
            free_mensaje(fin);
            log_destroy(log);
            close(fds[0]); close(fds[1]);
        } end

    } end

}
