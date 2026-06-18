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

// Wrappers que descartan los parámetros de herencia — para tests que no los necesitan.
static int lock(uint32_t pid, int prio, const char* nombre) {
    int oe, np;
    return mutex_ks_lock(pid, prio, nombre, get_logger(), &oe, &np);
}
static int unlock(uint32_t pid, const char* nombre) {
    int pr;
    return mutex_ks_unlock(pid, nombre, get_logger(), &pr);
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
            should_int(lock(1, 3, "tl_libre")) be equal to(0);
        } end

        it("retorna -1 si el mutex no existe") {
            ensure_init();
            should_int(lock(1, 0, "no_existe_lock")) be equal to(-1);
        } end

    } end

    describe("mutex_ks_lock — caso bloqueado") {

        it("retorna 1 cuando el mutex ya está tomado") {
            ensure_init();
            mutex_ks_create("tl_bloqueado");
            lock(1, 2, "tl_bloqueado");

            should_int(lock(2, 3, "tl_bloqueado")) be equal to(1);
        } end

        it("retorna 1 para todos los procesos que llegan después del primero") {
            ensure_init();
            mutex_ks_create("tl_multibloq");
            lock(1, 2, "tl_multibloq");

            should_int(lock(2, 3, "tl_multibloq")) be equal to(1);
            should_int(lock(3, 4, "tl_multibloq")) be equal to(1);
        } end

    } end

    describe("mutex_ks_unlock — sin waiters") {

        it("retorna -1 cuando no hay waiters (mutex queda libre)") {
            ensure_init();
            mutex_ks_create("tu_sin_waiters");
            lock(1, 2, "tu_sin_waiters");

            should_int(unlock(1, "tu_sin_waiters")) be equal to(-1);
        } end

        it("retorna -1 si el proceso no es el owner") {
            ensure_init();
            mutex_ks_create("tu_owner_incorrecto");
            lock(1, 2, "tu_owner_incorrecto");

            should_int(unlock(99, "tu_owner_incorrecto")) be equal to(-1);
        } end

        it("retorna -1 si el mutex no existe") {
            ensure_init();
            should_int(unlock(1, "no_existe_unlock")) be equal to(-1);
        } end

    } end

    describe("mutex_ks_unlock — con waiters") {

        it("retorna el PID del primer waiter al liberar") {
            ensure_init();
            mutex_ks_create("tu_con_waiter");
            lock(1, 2, "tu_con_waiter");
            lock(2, 3, "tu_con_waiter");

            should_int(unlock(1, "tu_con_waiter")) be equal to(2);
        } end

        it("el nuevo owner puede liberar el mutex correctamente") {
            ensure_init();
            mutex_ks_create("tu_transferencia");
            lock(1, 2, "tu_transferencia");
            lock(2, 3, "tu_transferencia");

            unlock(1, "tu_transferencia");
            should_int(unlock(2, "tu_transferencia")) be equal to(-1);
        } end

        it("pid 1 no puede liberar después de haber transferido a pid 2") {
            ensure_init();
            mutex_ks_create("tu_no_reuso");
            lock(1, 2, "tu_no_reuso");
            lock(2, 3, "tu_no_reuso");

            unlock(1, "tu_no_reuso");
            should_int(unlock(1, "tu_no_reuso")) be equal to(-1);
        } end

    } end

    describe("orden FIFO con tres procesos") {

        it("desbloquea en orden de llegada y retorna los PIDs correctos") {
            ensure_init();
            mutex_ks_create("tfifo");
            lock(1, 3, "tfifo");
            lock(2, 3, "tfifo");
            lock(3, 3, "tfifo");

            should_int(unlock(1, "tfifo")) be equal to(2);
            should_int(unlock(2, "tfifo")) be equal to(3);
            should_int(unlock(3, "tfifo")) be equal to(-1);
        } end

    } end

    describe("herencia de prioridades — señal de elevación") {

        it("no activa herencia cuando el waiter tiene menor prioridad que el owner") {
            // owner prioridad 2, waiter prioridad 5: 5 > 2 → sin herencia
            ensure_init();
            mutex_ks_create("th_sin_herencia");
            lock(10, 2, "th_sin_herencia");

            int owner_a_elevar = 0, nueva_prio = 0;
            mutex_ks_lock(11, 5, "th_sin_herencia", get_logger(),
                          &owner_a_elevar, &nueva_prio);
            should_int(owner_a_elevar) be equal to(-1);
        } end

        it("no activa herencia cuando el waiter tiene la misma prioridad que el owner") {
            ensure_init();
            mutex_ks_create("th_igual_prio");
            lock(20, 3, "th_igual_prio");

            int owner_a_elevar = 0, nueva_prio = 0;
            mutex_ks_lock(21, 3, "th_igual_prio", get_logger(),
                          &owner_a_elevar, &nueva_prio);
            should_int(owner_a_elevar) be equal to(-1);
        } end

        it("activa herencia cuando el waiter tiene mayor prioridad que el owner") {
            // owner prioridad 5, waiter prioridad 2: 2 < 5 → elevar owner al 2
            ensure_init();
            mutex_ks_create("th_con_herencia");
            lock(30, 5, "th_con_herencia");

            int owner_a_elevar = -1, nueva_prio = -1;
            mutex_ks_lock(31, 2, "th_con_herencia", get_logger(),
                          &owner_a_elevar, &nueva_prio);
            should_int(owner_a_elevar) be equal to(30);
            should_int(nueva_prio)     be equal to(2);
        } end

        it("activa herencia con prioridad máxima de los dos waiters sucesivos") {
            // owner prio 10, waiter1 prio 7, waiter2 prio 3: ambos elevan (progresivamente)
            ensure_init();
            mutex_ks_create("th_dos_waiters");
            lock(40, 10, "th_dos_waiters");

            int oe1, np1, oe2, np2;
            mutex_ks_lock(41, 7, "th_dos_waiters", get_logger(), &oe1, &np1);
            mutex_ks_lock(42, 3, "th_dos_waiters", get_logger(), &oe2, &np2);

            // primer waiter: 7 < 10 → elevar a 7
            should_int(oe1) be equal to(40);
            should_int(np1) be equal to(7);
            // segundo waiter: 3 < 10 (original) → elevar a 3
            should_int(oe2) be equal to(40);
            should_int(np2) be equal to(3);
        } end

    } end

    describe("herencia de prioridades — restauración en unlock") {

        it("unlock devuelve la prioridad original del owner sin herencia") {
            ensure_init();
            mutex_ks_create("tr_sin_herencia");
            lock(50, 4, "tr_sin_herencia");

            int prioridad_restaurar = 0;
            mutex_ks_unlock(50, "tr_sin_herencia", get_logger(), &prioridad_restaurar);
            should_int(prioridad_restaurar) be equal to(4);
        } end

        it("unlock devuelve la prioridad original aunque el owner haya sido elevado") {
            // owner prio 5, waiter prio 1 → owner elevado a 1; al unlock restaurar a 5
            ensure_init();
            mutex_ks_create("tr_con_herencia");
            lock(60, 5, "tr_con_herencia");
            lock(61, 1, "tr_con_herencia");

            int prioridad_restaurar = 0;
            mutex_ks_unlock(60, "tr_con_herencia", get_logger(), &prioridad_restaurar);
            should_int(prioridad_restaurar) be equal to(5);
        } end

        it("el nuevo owner usa su prioridad como prioridad_original tras la transferencia") {
            // owner(70, prio 5), waiter(71, prio 2) → 71 toma mutex con original=2
            // luego waiter(72, prio 4): 4 > 2 → sin herencia
            ensure_init();
            mutex_ks_create("tr_nuevo_owner");
            lock(70, 5, "tr_nuevo_owner");
            lock(71, 2, "tr_nuevo_owner");

            // 70 libera → 71 es el nuevo owner con original=2
            unlock(70, "tr_nuevo_owner");

            // ahora 72 con prio 4 intenta bloquear: 4 > 2 → no debe activar herencia
            int oe, np;
            mutex_ks_lock(72, 4, "tr_nuevo_owner", get_logger(), &oe, &np);
            should_int(oe) be equal to(-1);
        } end

        it("unlock devuelve -1 en prioridad_restaurar si el mutex no existe") {
            ensure_init();
            int pr = 99;
            mutex_ks_unlock(1, "no_existe_restaurar", get_logger(), &pr);
            should_int(pr) be equal to(-1);
        } end

    } end

}
