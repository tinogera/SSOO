#include <cspecs/cspec.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <unistd.h>
#include <stdlib.h>
#include <commons/log.h>
#include <utils/protocolo.h>
#include <utils/sockets.h>
#include "ks_mutex.h"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static t_log* test_logger = NULL;

static t_log* get_logger(void) {
    if (test_logger == NULL)
        test_logger = log_create("/tmp/ks_mutex_test.log", "test", false, LOG_LEVEL_ERROR);
    return test_logger;
}

// Inicializar la lista global una sola vez para todo el proceso de tests.
// Cada test usa nombres únicos, así no hay colisiones entre casos.
static int initialized = 0;
static void ensure_init(void) {
    if (!initialized) {
        mutexes_init();
        initialized = 1;
    }
}

// Verifica que un fd de socket no tenga datos disponibles para leer.
static int fd_vacio(int fd) {
    fd_set r;
    FD_ZERO(&r);
    FD_SET(fd, &r);
    struct timeval tv = {0, 0};
    return select(fd + 1, &r, NULL, NULL, &tv) == 0;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

context(ks_mutex) {

    describe("mutex_ks_create") {

        it("devuelve 0 al crear un mutex nuevo") {
            ensure_init();
            should_int(mutex_ks_create("tc_nuevo")) be equal to(0);
        } end

        it("devuelve -1 si el mutex ya existe") {
            ensure_init();
            mutex_ks_create("tc_dup");
            should_int(mutex_ks_create("tc_dup")) be equal to(-1);
        } end

    } end

    describe("mutex_ks_lock — caso libre") {

        it("toma el mutex y envía MSG_OK al fd") {
            ensure_init();
            int fds[2];
            socketpair(AF_UNIX, SOCK_STREAM, 0, fds);

            mutex_ks_create("tl_libre");
            int ret = mutex_ks_lock(1, fds[1], "tl_libre", get_logger());
            should_int(ret) be equal to(0);

            t_mensaje* ok = recibir_mensaje(fds[0]);
            should_ptr(ok) not be null;
            should_int(ok->op_code) be equal to(MSG_OK);

            free_mensaje(ok);
            close(fds[0]); close(fds[1]);
        } end

        it("devuelve -1 si el mutex no existe") {
            ensure_init();
            int fds[2];
            socketpair(AF_UNIX, SOCK_STREAM, 0, fds);

            int ret = mutex_ks_lock(1, fds[1], "no_existe_lock", get_logger());
            should_int(ret) be equal to(-1);

            close(fds[0]); close(fds[1]);
        } end

    } end

    describe("mutex_ks_lock — caso bloqueado") {

        it("devuelve 1 y no envía MSG_OK al segundo proceso") {
            ensure_init();
            int fds1[2], fds2[2];
            socketpair(AF_UNIX, SOCK_STREAM, 0, fds1);
            socketpair(AF_UNIX, SOCK_STREAM, 0, fds2);

            mutex_ks_create("tl_bloqueado");
            mutex_ks_lock(1, fds1[1], "tl_bloqueado", get_logger());
            t_mensaje* ok1 = recibir_mensaje(fds1[0]);
            free_mensaje(ok1);

            int ret = mutex_ks_lock(2, fds2[1], "tl_bloqueado", get_logger());
            should_int(ret) be equal to(1);

            // pid 2 no debe recibir nada todavía
            should_bool(fd_vacio(fds2[0])) be truthy;

            close(fds1[0]); close(fds1[1]);
            close(fds2[0]); close(fds2[1]);
        } end

    } end

    describe("mutex_ks_unlock") {

        it("libera el mutex sin waiters y devuelve 0") {
            ensure_init();
            int fds[2];
            socketpair(AF_UNIX, SOCK_STREAM, 0, fds);

            mutex_ks_create("tu_sin_waiters");
            mutex_ks_lock(1, fds[1], "tu_sin_waiters", get_logger());
            t_mensaje* ok = recibir_mensaje(fds[0]);
            free_mensaje(ok);

            should_int(mutex_ks_unlock(1, "tu_sin_waiters", get_logger())) be equal to(0);

            close(fds[0]); close(fds[1]);
        } end

        it("transfiere la propiedad al primer waiter y le envía MSG_OK") {
            ensure_init();
            int fds1[2], fds2[2];
            socketpair(AF_UNIX, SOCK_STREAM, 0, fds1);
            socketpair(AF_UNIX, SOCK_STREAM, 0, fds2);

            mutex_ks_create("tu_con_waiter");
            mutex_ks_lock(1, fds1[1], "tu_con_waiter", get_logger());
            t_mensaje* ok1 = recibir_mensaje(fds1[0]);
            free_mensaje(ok1);

            mutex_ks_lock(2, fds2[1], "tu_con_waiter", get_logger());
            mutex_ks_unlock(1, "tu_con_waiter", get_logger());

            t_mensaje* ok2 = recibir_mensaje(fds2[0]);
            should_ptr(ok2) not be null;
            should_int(ok2->op_code) be equal to(MSG_OK);

            free_mensaje(ok2);
            close(fds1[0]); close(fds1[1]);
            close(fds2[0]); close(fds2[1]);
        } end

        it("devuelve -1 si el proceso no es el owner") {
            ensure_init();
            int fds[2];
            socketpair(AF_UNIX, SOCK_STREAM, 0, fds);

            mutex_ks_create("tu_owner_incorrecto");
            mutex_ks_lock(1, fds[1], "tu_owner_incorrecto", get_logger());
            t_mensaje* ok = recibir_mensaje(fds[0]);
            free_mensaje(ok);

            should_int(mutex_ks_unlock(99, "tu_owner_incorrecto", get_logger())) be equal to(-1);

            close(fds[0]); close(fds[1]);
        } end

        it("devuelve -1 si el mutex no existe") {
            ensure_init();
            should_int(mutex_ks_unlock(1, "no_existe_unlock", get_logger())) be equal to(-1);
        } end

    } end

    describe("orden FIFO con tres procesos") {

        it("desbloquea en orden de llegada") {
            ensure_init();
            int fds1[2], fds2[2], fds3[2];
            socketpair(AF_UNIX, SOCK_STREAM, 0, fds1);
            socketpair(AF_UNIX, SOCK_STREAM, 0, fds2);
            socketpair(AF_UNIX, SOCK_STREAM, 0, fds3);

            mutex_ks_create("tfifo");
            // pid 1 toma
            mutex_ks_lock(1, fds1[1], "tfifo", get_logger());
            t_mensaje* ok1 = recibir_mensaje(fds1[0]);
            free_mensaje(ok1);

            // pid 2 y pid 3 quedan en cola (en ese orden)
            mutex_ks_lock(2, fds2[1], "tfifo", get_logger());
            mutex_ks_lock(3, fds3[1], "tfifo", get_logger());

            // pid 1 suelta → pid 2 recibe MSG_OK (FIFO)
            mutex_ks_unlock(1, "tfifo", get_logger());
            t_mensaje* ok2 = recibir_mensaje(fds2[0]);
            should_ptr(ok2) not be null;
            should_int(ok2->op_code) be equal to(MSG_OK);
            free_mensaje(ok2);

            // pid 3 todavía no debe haber recibido nada
            should_bool(fd_vacio(fds3[0])) be truthy;

            // pid 2 suelta → pid 3 recibe MSG_OK
            mutex_ks_unlock(2, "tfifo", get_logger());
            t_mensaje* ok3 = recibir_mensaje(fds3[0]);
            should_ptr(ok3) not be null;
            should_int(ok3->op_code) be equal to(MSG_OK);
            free_mensaje(ok3);

            close(fds1[0]); close(fds1[1]);
            close(fds2[0]); close(fds2[1]);
            close(fds3[0]); close(fds3[1]);
        } end

    } end

}
