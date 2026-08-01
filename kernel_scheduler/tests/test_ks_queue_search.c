#include <cspecs/cspec.h>
#include <stdlib.h>
#include <commons/collections/queue.h>
#include <commons/log.h>
#include "proceso.h"
#include "ks_queue_search.h"
#include "ks_mutex.h"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static t_log* test_logger(void) {
    static t_log* l = NULL;
    if (!l) l = log_create("/tmp/ks_queue_search_test.log", "test", false, LOG_LEVEL_ERROR);
    return l;
}

static t_proceso* crear_proceso(int pid, int prioridad) {
    t_proceso* p = malloc(sizeof(t_proceso));
    p->PID              = pid;
    p->estado           = BLOCK;
    p->prioridad        = prioridad;
    p->fd_cpu           = -1;
    p->preemptado       = 0;
    p->gen_despacho     = 0;
    p->gen_bloqueo      = 0;
    p->esperando_stdin  = 0;
    p->tiempo_suspension = 0;
    return p;
}

static int initialized = 0;
static void ensure_mutexes_init(void) {
    if (!initialized) {
        mutexes_init();
        initialized = 1;
    }
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

context(ks_queue_search) {

    describe("buscar_pid_en_queue") {

        it("encuentra un proceso presente en la cola") {
            t_queue* cola = queue_create();
            t_proceso* p1 = crear_proceso(1, 3);
            t_proceso* p2 = crear_proceso(2, 5);
            queue_push(cola, p1);
            queue_push(cola, p2);

            t_proceso* encontrado = buscar_pid_en_queue(cola, 2);

            should_ptr(encontrado) not be null;
            should_int(encontrado->PID) be equal to(2);

            free(p1); free(p2);
            queue_destroy(cola);
        } end

        it("devuelve NULL si el pid no está en la cola") {
            t_queue* cola = queue_create();
            t_proceso* p1 = crear_proceso(1, 3);
            queue_push(cola, p1);

            should_ptr(buscar_pid_en_queue(cola, 99)) be null;

            free(p1);
            queue_destroy(cola);
        } end

        it("devuelve NULL con la cola vacía") {
            t_queue* cola = queue_create();
            should_ptr(buscar_pid_en_queue(cola, 1)) be null;
            queue_destroy(cola);
        } end

        it("no saca al proceso de la cola (queue_size no cambia)") {
            t_queue* cola = queue_create();
            t_proceso* p1 = crear_proceso(1, 3);
            queue_push(cola, p1);

            buscar_pid_en_queue(cola, 1);

            should_int((int)queue_size(cola)) be equal to(1);

            free(p1);
            queue_destroy(cola);
        } end

        it("encuentra al primero que matchea si hay pids repetidos") {
            t_queue* cola = queue_create();
            t_proceso* p1 = crear_proceso(7, 1);
            t_proceso* p2 = crear_proceso(7, 9);
            queue_push(cola, p1);
            queue_push(cola, p2);

            t_proceso* encontrado = buscar_pid_en_queue(cola, 7);
            should_ptr(encontrado) be equal to(p1);

            free(p1); free(p2);
            queue_destroy(cola);
        } end

    } end

    describe("herencia de prioridades con el owner bloqueado (regresión)") {

        // Reproduce el escenario que buscar_proceso_activo no cubría antes del
        // fix: el owner del mutex está en una cola de BLOCK (no en EXEC ni en
        // READY) cuando llega un waiter de mayor prioridad. Este test simula
        // el mismo flujo que hace kernel_scheduler/src/main.c en el handler
        // MSG_MUTEX_LOCK — mutex_ks_lock() para la señal de herencia, y
        // buscar_pid_en_queue() (la misma función que ahora usa
        // buscar_proceso_activo para revisar cola_block) para localizar al
        // owner y aplicarle la nueva prioridad.

        it("localiza al owner bloqueado y le eleva la prioridad") {
            ensure_mutexes_init();
            mutex_ks_create("mtx_owner_bloqueado");

            // Owner (pid 100, prioridad 5) toma el mutex y luego se bloquea
            // (p.ej. hizo SLEEP sin soltar el mutex).
            int oe, np;
            mutex_ks_lock(100, 5, "mtx_owner_bloqueado", test_logger(), &oe, &np);
            should_int(oe) be equal to(-1); // mutex estaba libre, sin herencia todavía

            t_queue* cola_block_simulada = queue_create();
            t_proceso* owner = crear_proceso(100, 5);
            queue_push(cola_block_simulada, owner);

            // Waiter de mayor prioridad (pid 200, prioridad 1) pide el mismo mutex.
            int owner_a_elevar = -1, nueva_prioridad_owner = -1;
            int resultado = mutex_ks_lock(200, 1, "mtx_owner_bloqueado", test_logger(),
                                          &owner_a_elevar, &nueva_prioridad_owner);

            should_int(resultado)              be equal to(1);  // quedó como waiter
            should_int(owner_a_elevar)         be equal to(100);
            should_int(nueva_prioridad_owner)  be equal to(1);

            // Antes del fix, buscar_proceso_activo no miraba cola_block y esto
            // hubiera sido NULL, dejando al owner con su prioridad original.
            t_proceso* encontrado = buscar_pid_en_queue(cola_block_simulada, owner_a_elevar);
            should_ptr(encontrado) not be null;

            if (encontrado && encontrado->prioridad > nueva_prioridad_owner) {
                encontrado->prioridad = nueva_prioridad_owner;
            }

            should_int(owner->prioridad) be equal to(1);

            free(owner);
            queue_destroy(cola_block_simulada);
        } end

    } end

}
