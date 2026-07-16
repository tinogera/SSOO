#include <cspecs/cspec.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "io_utils.h"

// Crea un t_mensaje de MSG_IO_SLEEP listo para pasar a manejar_sleep.
// manejar_sleep llama free_mensaje internamente, así que no liberar después.
static t_mensaje* crear_msg_sleep(uint32_t pid, uint32_t tiempo_ms) {
    t_mensaje* msg        = malloc(sizeof(t_mensaje));
    t_payload_io_sleep* p = malloc(sizeof(t_payload_io_sleep));
    p->pid       = htonl(pid);
    p->tiempo_ms = htonl(tiempo_ms);
    msg->op_code      = MSG_IO_SLEEP;
    msg->payload_size = sizeof(t_payload_io_sleep);
    msg->payload      = p;
    return msg;
}

// Crea un t_mensaje de MSG_IO_STDIN listo para pasar a manejar_stdin.
// manejar_stdin llama free_mensaje internamente, así que no liberar después.
static t_mensaje* crear_msg_stdin(uint32_t pid, uint32_t n_bytes) {
    t_mensaje* msg        = malloc(sizeof(t_mensaje));
    t_payload_io_stdin* p = malloc(sizeof(t_payload_io_stdin));
    p->pid     = htonl(pid);
    p->n_bytes = htonl(n_bytes);
    msg->op_code      = MSG_IO_STDIN;
    msg->payload_size = sizeof(t_payload_io_stdin);
    msg->payload      = p;
    return msg;
}

// Crea un t_mensaje de MSG_IO_STDOUT con pid y contenido dados.
// payload = { uint32_t pid | char contenido[len] } — sin '\0' final,
// igual que lo enviaría el KS real (manejar_stdout lo agrega internamente).
static t_mensaje* crear_msg_stdout(uint32_t pid, const char* contenido) {
    size_t len            = strlen(contenido);
    uint32_t payload_size = sizeof(uint32_t) + len;
    void* p               = malloc(payload_size);
    memcpy(p, &pid, sizeof(uint32_t));
    memcpy((char*)p + sizeof(uint32_t), contenido, len);
    t_mensaje* msg    = malloc(sizeof(t_mensaje));
    msg->op_code      = MSG_IO_STDOUT;
    msg->payload_size = payload_size;
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
            t_log* log = log_create("/tmp/io_test.log", "test", false, LOG_LEVEL_ERROR);

            manejar_sleep(crear_msg_sleep(99, 1), fds[1], log);

            t_mensaje* fin = recibir_mensaje(fds[0]);
            should_ptr(fin) not be null;
            should_int(fin->op_code) be equal to(MSG_IO_FIN);

            t_payload_io_fin* payload = (t_payload_io_fin*) fin->payload;
            should_int(ntohl(payload->pid)) be equal to(99);

            free_mensaje(fin);
            log_destroy(log);
            close(fds[0]); close(fds[1]);
        } end

        it ("no confunde el PID cuando se llama dos veces seguidas") {
            int fds[2];
            socketpair(AF_UNIX, SOCK_STREAM, 0, fds);
            t_log* log = log_create("/tmp/io_test.log", "test", false, LOG_LEVEL_ERROR);

            manejar_sleep(crear_msg_sleep(1, 1), fds[1], log);
            manejar_sleep(crear_msg_sleep(2, 1), fds[1], log);

            t_mensaje* fin1 = recibir_mensaje(fds[0]);
            t_mensaje* fin2 = recibir_mensaje(fds[0]);

            should_int(ntohl(((t_payload_io_fin*)fin1->payload)->pid)) be equal to(1);
            should_int(ntohl(((t_payload_io_fin*)fin2->payload)->pid)) be equal to(2);

            free_mensaje(fin1); free_mensaje(fin2);
            log_destroy(log);
            close(fds[0]); close(fds[1]);
        } end

        it ("duerme al menos el tiempo solicitado") {
            int fds[2];
            socketpair(AF_UNIX, SOCK_STREAM, 0, fds);
            t_log* log = log_create("/tmp/io_test.log", "test", false, LOG_LEVEL_ERROR);

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

    describe ("manejar_stdout") {

        it ("envía MSG_IO_FIN con el PID correcto al terminar") {
            int fds[2];
            socketpair(AF_UNIX, SOCK_STREAM, 0, fds);
            t_log* log = log_create("/tmp/io_test.log", "test", false, LOG_LEVEL_ERROR);

            manejar_stdout(crear_msg_stdout(42, "Hola mundo"), fds[1], log);

            t_mensaje* fin = recibir_mensaje(fds[0]);
            should_ptr(fin) not be null;
            should_int(fin->op_code) be equal to(MSG_IO_FIN);
            should_int(ntohl(((t_payload_io_fin*)fin->payload)->pid)) be equal to(42);

            free_mensaje(fin);
            log_destroy(log);
            close(fds[0]); close(fds[1]);
        } end

        it ("no confunde el PID cuando se llama dos veces seguidas") {
            int fds[2];
            socketpair(AF_UNIX, SOCK_STREAM, 0, fds);
            t_log* log = log_create("/tmp/io_test.log", "test", false, LOG_LEVEL_ERROR);

            manejar_stdout(crear_msg_stdout(10, "primero"), fds[1], log);
            manejar_stdout(crear_msg_stdout(20, "segundo"), fds[1], log);

            t_mensaje* fin1 = recibir_mensaje(fds[0]);
            t_mensaje* fin2 = recibir_mensaje(fds[0]);
            should_int(ntohl(((t_payload_io_fin*)fin1->payload)->pid)) be equal to(10);
            should_int(ntohl(((t_payload_io_fin*)fin2->payload)->pid)) be equal to(20);

            free_mensaje(fin1); free_mensaje(fin2);
            log_destroy(log);
            close(fds[0]); close(fds[1]);
        } end

        it ("funciona con contenido vacío") {
            int fds[2];
            socketpair(AF_UNIX, SOCK_STREAM, 0, fds);
            t_log* log = log_create("/tmp/io_test.log", "test", false, LOG_LEVEL_ERROR);

            manejar_stdout(crear_msg_stdout(7, ""), fds[1], log);

            t_mensaje* fin = recibir_mensaje(fds[0]);
            should_ptr(fin) not be null;
            should_int(fin->op_code) be equal to(MSG_IO_FIN);
            should_int(ntohl(((t_payload_io_fin*)fin->payload)->pid)) be equal to(7);

            free_mensaje(fin);
            log_destroy(log);
            close(fds[0]); close(fds[1]);
        } end

    } end

    describe ("manejar_stdin") {

        // Para simular la entrada del teclado se redirige stdin al extremo de
        // lectura de un pipe. Se escribe la entrada en el extremo de escritura,
        // se cierra ese extremo para que fgets reciba EOF al agotar los bytes,
        // y al terminar el test se restaura stdin al file descriptor original.

        it ("envía MSG_IO_STDIN_DATOS con el PID y n_bytes correctos") {
            int fds_ks[2], pipe_stdin[2];
            socketpair(AF_UNIX, SOCK_STREAM, 0, fds_ks);
            pipe(pipe_stdin);
            t_log* log = log_create("/tmp/io_test.log", "test", false, LOG_LEVEL_ERROR);

            write(pipe_stdin[1], "hola\n", 5);
            close(pipe_stdin[1]);

            int stdin_orig = dup(STDIN_FILENO);
            dup2(pipe_stdin[0], STDIN_FILENO);
            close(pipe_stdin[0]);

            manejar_stdin(crear_msg_stdin(55, 4), fds_ks[1], log);

            dup2(stdin_orig, STDIN_FILENO);
            close(stdin_orig);

            t_mensaje* respuesta = recibir_mensaje(fds_ks[0]);
            should_ptr(respuesta) not be null;
            should_int(respuesta->op_code) be equal to(MSG_IO_STDIN_DATOS);

            uint32_t pid_recv, n_recv;
            memcpy(&pid_recv, respuesta->payload, sizeof(uint32_t));
            memcpy(&n_recv, (char*)respuesta->payload + sizeof(uint32_t), sizeof(uint32_t));
            should_int(pid_recv) be equal to(55);
            should_int(n_recv)   be equal to(4);

            free_mensaje(respuesta);
            log_destroy(log);
            close(fds_ks[0]); close(fds_ks[1]);
        } end

        it ("trunca la entrada si el usuario escribe más de n_bytes") {
            int fds_ks[2], pipe_stdin[2];
            socketpair(AF_UNIX, SOCK_STREAM, 0, fds_ks);
            pipe(pipe_stdin);
            t_log* log = log_create("/tmp/io_test.log", "test", false, LOG_LEVEL_ERROR);

            write(pipe_stdin[1], "abcdefgh\n", 9);
            close(pipe_stdin[1]);

            int stdin_orig = dup(STDIN_FILENO);
            dup2(pipe_stdin[0], STDIN_FILENO);
            close(pipe_stdin[0]);

            manejar_stdin(crear_msg_stdin(1, 4), fds_ks[1], log);

            dup2(stdin_orig, STDIN_FILENO);
            close(stdin_orig);

            t_mensaje* respuesta = recibir_mensaje(fds_ks[0]);
            char* datos = (char*)respuesta->payload + sizeof(uint32_t) + sizeof(uint32_t);
            should_int(datos[0]) be equal to('a');
            should_int(datos[1]) be equal to('b');
            should_int(datos[2]) be equal to('c');
            should_int(datos[3]) be equal to('d');

            free_mensaje(respuesta);
            log_destroy(log);
            close(fds_ks[0]); close(fds_ks[1]);
        } end

        it ("rellena con ceros si el usuario escribe menos de n_bytes") {
            int fds_ks[2], pipe_stdin[2];
            socketpair(AF_UNIX, SOCK_STREAM, 0, fds_ks);
            pipe(pipe_stdin);
            t_log* log = log_create("/tmp/io_test.log", "test", false, LOG_LEVEL_ERROR);

            write(pipe_stdin[1], "ab\n", 3);
            close(pipe_stdin[1]);

            int stdin_orig = dup(STDIN_FILENO);
            dup2(pipe_stdin[0], STDIN_FILENO);
            close(pipe_stdin[0]);

            manejar_stdin(crear_msg_stdin(2, 5), fds_ks[1], log);

            dup2(stdin_orig, STDIN_FILENO);
            close(stdin_orig);

            t_mensaje* respuesta = recibir_mensaje(fds_ks[0]);
            char* datos = (char*)respuesta->payload + sizeof(uint32_t) + sizeof(uint32_t);
            should_int(datos[0]) be equal to('a');
            should_int(datos[1]) be equal to('b');
            should_int(datos[2]) be equal to(0);
            should_int(datos[3]) be equal to(0);
            should_int(datos[4]) be equal to(0);

            free_mensaje(respuesta);
            log_destroy(log);
            close(fds_ks[0]); close(fds_ks[1]);
        } end

    } end

}
