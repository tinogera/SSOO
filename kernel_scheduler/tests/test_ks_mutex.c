#include <cspecs/cspec.h>
#include <stdlib.h>
#include <commons/log.h>
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

static int initialized = 0;
static void ensure_init(void) {
    if (!initialized) {
        mutexes_init();
        initialized = 1;
    }
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

        it("retorna 0 cuando el mutex está libre") {
            ensure_init();
            mutex_ks_create("tl_libre");
            should_int(mutex_ks_lock(1, "tl_libre", get_logger())) be equal to(0);
        } end

        it("retorna -1 si el mutex no existe") {
            ensure_init();
            should_int(mutex_ks_lock(1, "no_existe_lock", get_logger())) be equal to(-1);
        } end

    } end

    describe("mutex_ks_lock — caso bloqueado") {

        it("retorna 1 cuando el mutex ya está tomado") {
            ensure_init();
            mutex_ks_create("tl_bloqueado");
            mutex_ks_lock(1, "tl_bloqueado", get_logger());

            should_int(mutex_ks_lock(2, "tl_bloqueado", get_logger())) be equal to(1);
        } end

        it("retorna 1 para todos los procesos que llegan después del primero") {
            ensure_init();
            mutex_ks_create("tl_multibloq");
            mutex_ks_lock(1, "tl_multibloq", get_logger());

            should_int(mutex_ks_lock(2, "tl_multibloq", get_logger())) be equal to(1);
            should_int(mutex_ks_lock(3, "tl_multibloq", get_logger())) be equal to(1);
        } end

    } end

    describe("mutex_ks_unlock — sin waiters") {

        it("retorna -1 cuando no hay waiters (mutex queda libre)") {
            ensure_init();
            mutex_ks_create("tu_sin_waiters");
            mutex_ks_lock(1, "tu_sin_waiters", get_logger());

            should_int(mutex_ks_unlock(1, "tu_sin_waiters", get_logger())) be equal to(-1);
        } end

        it("retorna -1 si el proceso no es el owner") {
            ensure_init();
            mutex_ks_create("tu_owner_incorrecto");
            mutex_ks_lock(1, "tu_owner_incorrecto", get_logger());

            should_int(mutex_ks_unlock(99, "tu_owner_incorrecto", get_logger())) be equal to(-1);
        } end

        it("retorna -1 si el mutex no existe") {
            ensure_init();
            should_int(mutex_ks_unlock(1, "no_existe_unlock", get_logger())) be equal to(-1);
        } end

    } end

    describe("mutex_ks_unlock — con waiters") {

        it("retorna el PID del primer waiter al liberar") {
            ensure_init();
            mutex_ks_create("tu_con_waiter");
            mutex_ks_lock(1, "tu_con_waiter", get_logger());
            mutex_ks_lock(2, "tu_con_waiter", get_logger());

            should_int(mutex_ks_unlock(1, "tu_con_waiter", get_logger())) be equal to(2);
        } end

        it("el nuevo owner puede liberar el mutex correctamente") {
            ensure_init();
            mutex_ks_create("tu_transferencia");
            mutex_ks_lock(1, "tu_transferencia", get_logger());
            mutex_ks_lock(2, "tu_transferencia", get_logger());

            mutex_ks_unlock(1, "tu_transferencia", get_logger());
            // pid 2 es el nuevo owner — puede liberar
            should_int(mutex_ks_unlock(2, "tu_transferencia", get_logger())) be equal to(-1);
        } end

        it("pid 1 no puede liberar después de haber transferido a pid 2") {
            ensure_init();
            mutex_ks_create("tu_no_reuso");
            mutex_ks_lock(1, "tu_no_reuso", get_logger());
            mutex_ks_lock(2, "tu_no_reuso", get_logger());

            mutex_ks_unlock(1, "tu_no_reuso", get_logger());
            // pid 1 ya no es owner: debe retornar -1
            should_int(mutex_ks_unlock(1, "tu_no_reuso", get_logger())) be equal to(-1);
        } end

    } end

    describe("orden FIFO con tres procesos") {

        it("desbloquea en orden de llegada y retorna los PIDs correctos") {
            ensure_init();
            mutex_ks_create("tfifo");
            mutex_ks_lock(1, "tfifo", get_logger());
            mutex_ks_lock(2, "tfifo", get_logger());
            mutex_ks_lock(3, "tfifo", get_logger());

            // pid 1 suelta → debe retornar pid 2 (primero en cola)
            should_int(mutex_ks_unlock(1, "tfifo", get_logger())) be equal to(2);

            // pid 2 suelta → debe retornar pid 3
            should_int(mutex_ks_unlock(2, "tfifo", get_logger())) be equal to(3);

            // pid 3 suelta → no hay más waiters
            should_int(mutex_ks_unlock(3, "tfifo", get_logger())) be equal to(-1);
        } end

    } end

}
