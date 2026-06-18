#include <cspecs/cspec.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>
#include <commons/log.h>
#include "ks_compact.h"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static t_log* test_logger(void) {
    static t_log* l = NULL;
    if (!l) l = log_create("/tmp/ks_compact_test.log", "test", false, LOG_LEVEL_ERROR);
    return l;
}

// Crea un socketpair y devuelve fds[0]=lado_km, fds[1]=lado_ks
static void mk_pair(int fds[2]) {
    socketpair(AF_UNIX, SOCK_STREAM, 0, fds);
}

// Flag atomizable para verificar que on_compactar fue llamado
static volatile int handler_invocaciones = 0;
static void mock_compactar(void) {
    handler_invocaciones++;
}

// Hilo auxiliar: arranca km_listener_run con el ctx dado y termina cuando
// el fd se cierra.
static void* hilo_listener(void* ctx) {
    return km_listener_run(ctx);
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

context(ks_compact) {

    describe("km_listener_run — routing de mensajes no-COMPACTAR") {

        it("MSG_OK deposita el mensaje en *ultimo_resp_km y señala la cond") {
            int fds[2];
            mk_pair(fds);
            t_mensaje*      respuesta   = NULL;
            pthread_mutex_t mu          = PTHREAD_MUTEX_INITIALIZER;
            pthread_cond_t  cv          = PTHREAD_COND_INITIALIZER;

            t_km_listener_ctx ctx = {
                .fd_km              = fds[1],
                .on_compactar       = NULL,
                .mutex_km_resp      = &mu,
                .cond_km_resp       = &cv,
                .ultimo_resp_km_ptr = &respuesta,
                .logger             = test_logger(),
            };

            pthread_t t;
            pthread_create(&t, NULL, hilo_listener, &ctx);

            // Enviar MSG_OK desde el lado KM
            enviar_mensaje(fds[0], MSG_OK, NULL, 0);

            pthread_mutex_lock(&mu);
            while (respuesta == NULL) pthread_cond_wait(&cv, &mu);
            pthread_mutex_unlock(&mu);

            should_ptr(respuesta) not be null;
            should_int(respuesta->op_code) be equal to(MSG_OK);

            free_mensaje(respuesta);
            close(fds[0]); close(fds[1]);
            pthread_join(t, NULL);
        } end

        it("mensaje desconocido también se deposita en *ultimo_resp_km") {
            int fds[2];
            mk_pair(fds);
            t_mensaje*      respuesta = NULL;
            pthread_mutex_t mu        = PTHREAD_MUTEX_INITIALIZER;
            pthread_cond_t  cv        = PTHREAD_COND_INITIALIZER;

            t_km_listener_ctx ctx = {
                .fd_km              = fds[1],
                .on_compactar       = NULL,
                .mutex_km_resp      = &mu,
                .cond_km_resp       = &cv,
                .ultimo_resp_km_ptr = &respuesta,
                .logger             = test_logger(),
            };

            pthread_t t;
            pthread_create(&t, NULL, hilo_listener, &ctx);

            enviar_mensaje(fds[0], MSG_ERROR, NULL, 0);

            pthread_mutex_lock(&mu);
            while (respuesta == NULL) pthread_cond_wait(&cv, &mu);
            pthread_mutex_unlock(&mu);

            should_int(respuesta->op_code) be equal to(MSG_ERROR);

            free_mensaje(respuesta);
            close(fds[0]); close(fds[1]);
            pthread_join(t, NULL);
        } end

        it("dos respuestas sucesivas llegan en orden") {
            int fds[2];
            mk_pair(fds);
            t_mensaje*      resp1 = NULL;
            t_mensaje*      resp2 = NULL;
            pthread_mutex_t mu    = PTHREAD_MUTEX_INITIALIZER;
            pthread_cond_t  cv    = PTHREAD_COND_INITIALIZER;

            t_km_listener_ctx ctx = {
                .fd_km              = fds[1],
                .on_compactar       = NULL,
                .mutex_km_resp      = &mu,
                .cond_km_resp       = &cv,
                .ultimo_resp_km_ptr = &resp1,
                .logger             = test_logger(),
            };

            pthread_t t;
            pthread_create(&t, NULL, hilo_listener, &ctx);

            enviar_mensaje(fds[0], MSG_OK, NULL, 0);
            pthread_mutex_lock(&mu);
            while (resp1 == NULL) pthread_cond_wait(&cv, &mu);
            pthread_mutex_unlock(&mu);

            // Reutilizar el ptr para el segundo mensaje
            ctx.ultimo_resp_km_ptr = &resp2;
            enviar_mensaje(fds[0], MSG_ERROR, NULL, 0);
            pthread_mutex_lock(&mu);
            while (resp2 == NULL) pthread_cond_wait(&cv, &mu);
            pthread_mutex_unlock(&mu);

            should_int(resp1->op_code) be equal to(MSG_OK);
            should_int(resp2->op_code) be equal to(MSG_ERROR);

            free_mensaje(resp1); free_mensaje(resp2);
            close(fds[0]); close(fds[1]);
            pthread_join(t, NULL);
        } end

    } end

    describe("km_listener_run — routing de MSG_COMPACTAR") {

        it("MSG_COMPACTAR invoca on_compactar y no deposita nada en ultimo_resp_km") {
            handler_invocaciones = 0;

            int fds[2];
            mk_pair(fds);
            t_mensaje*      respuesta = NULL;
            pthread_mutex_t mu        = PTHREAD_MUTEX_INITIALIZER;
            pthread_cond_t  cv        = PTHREAD_COND_INITIALIZER;

            t_km_listener_ctx ctx = {
                .fd_km              = fds[1],
                .on_compactar       = mock_compactar,
                .mutex_km_resp      = &mu,
                .cond_km_resp       = &cv,
                .ultimo_resp_km_ptr = &respuesta,
                .logger             = test_logger(),
            };

            pthread_t t;
            pthread_create(&t, NULL, hilo_listener, &ctx);

            enviar_mensaje(fds[0], MSG_COMPACTAR, NULL, 0);

            // Dar tiempo al listener para procesar
            struct timespec ts = { .tv_sec = 0, .tv_nsec = 10000000 };
            nanosleep(&ts, NULL);

            should_int(handler_invocaciones) be equal to(1);
            should_ptr(respuesta) be null;

            close(fds[0]); close(fds[1]);
            pthread_join(t, NULL);
        } end

        it("MSG_COMPACTAR seguido de MSG_OK: primero handler, luego depósito") {
            handler_invocaciones = 0;

            int fds[2];
            mk_pair(fds);
            t_mensaje*      respuesta = NULL;
            pthread_mutex_t mu        = PTHREAD_MUTEX_INITIALIZER;
            pthread_cond_t  cv        = PTHREAD_COND_INITIALIZER;

            t_km_listener_ctx ctx = {
                .fd_km              = fds[1],
                .on_compactar       = mock_compactar,
                .mutex_km_resp      = &mu,
                .cond_km_resp       = &cv,
                .ultimo_resp_km_ptr = &respuesta,
                .logger             = test_logger(),
            };

            pthread_t t;
            pthread_create(&t, NULL, hilo_listener, &ctx);

            enviar_mensaje(fds[0], MSG_COMPACTAR, NULL, 0);
            enviar_mensaje(fds[0], MSG_OK, NULL, 0);

            pthread_mutex_lock(&mu);
            while (respuesta == NULL) pthread_cond_wait(&cv, &mu);
            pthread_mutex_unlock(&mu);

            should_int(handler_invocaciones) be equal to(1);
            should_int(respuesta->op_code)   be equal to(MSG_OK);

            free_mensaje(respuesta);
            close(fds[0]); close(fds[1]);
            pthread_join(t, NULL);
        } end

        it("on_compactar NULL no produce crash con MSG_COMPACTAR") {
            int fds[2];
            mk_pair(fds);
            t_mensaje*      respuesta = NULL;
            pthread_mutex_t mu        = PTHREAD_MUTEX_INITIALIZER;
            pthread_cond_t  cv        = PTHREAD_COND_INITIALIZER;

            t_km_listener_ctx ctx = {
                .fd_km              = fds[1],
                .on_compactar       = NULL,   // sin handler
                .mutex_km_resp      = &mu,
                .cond_km_resp       = &cv,
                .ultimo_resp_km_ptr = &respuesta,
                .logger             = test_logger(),
            };

            pthread_t t;
            pthread_create(&t, NULL, hilo_listener, &ctx);

            // Sin handler, MSG_COMPACTAR no debe colgar ni crashear.
            // Enviamos MSG_OK a continuación para confirmar que el listener sigue vivo.
            enviar_mensaje(fds[0], MSG_COMPACTAR, NULL, 0);
            enviar_mensaje(fds[0], MSG_OK, NULL, 0);

            pthread_mutex_lock(&mu);
            while (respuesta == NULL) pthread_cond_wait(&cv, &mu);
            pthread_mutex_unlock(&mu);

            should_int(respuesta->op_code) be equal to(MSG_OK);

            free_mensaje(respuesta);
            close(fds[0]); close(fds[1]);
            pthread_join(t, NULL);
        } end

    } end

    describe("km_listener_run — cierre de conexión") {

        it("termina limpiamente cuando fd se cierra") {
            int fds[2];
            mk_pair(fds);
            t_mensaje*      respuesta = NULL;
            pthread_mutex_t mu        = PTHREAD_MUTEX_INITIALIZER;
            pthread_cond_t  cv        = PTHREAD_COND_INITIALIZER;

            t_km_listener_ctx ctx = {
                .fd_km              = fds[1],
                .on_compactar       = NULL,
                .mutex_km_resp      = &mu,
                .cond_km_resp       = &cv,
                .ultimo_resp_km_ptr = &respuesta,
                .logger             = test_logger(),
            };

            pthread_t t;
            pthread_create(&t, NULL, hilo_listener, &ctx);

            // Cerrar el lado KM fuerza recibir_mensaje a retornar NULL
            close(fds[0]);

            // El join no debe bloquearse: el listener debe haber terminado
            void* retval;
            pthread_join(t, &retval);
            // Si llegamos aquí sin timeout, el test pasa
            should_ptr(retval) be null;

            close(fds[1]);
        } end

    } end

}
