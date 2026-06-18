#ifndef KS_COMPACT_H_
#define KS_COMPACT_H_

#include <commons/log.h>
#include <utils/sockets.h>
#include <utils/protocolo.h>
#include <pthread.h>

typedef void (*t_compactar_fn)(void);

/*
 * Contexto para km_listener_run.
 * Permite instanciar el listener con cualquier fd y handler en tests.
 */
typedef struct {
    int              fd_km;
    t_compactar_fn   on_compactar;       // llamado cuando llega MSG_COMPACTAR
    pthread_mutex_t* mutex_km_resp;
    pthread_cond_t*  cond_km_resp;
    t_mensaje**      ultimo_resp_km_ptr; // *ptr = mensaje recibido; señalado por cond
    t_log*           logger;
} t_km_listener_ctx;

/*
 * Loop del listener de KM.
 * - MSG_COMPACTAR: llama ctx->on_compactar() y libera el mensaje.
 * - Cualquier otro mensaje: lo deposita en *ctx->ultimo_resp_km_ptr
 *   y señala ctx->cond_km_resp para despertar al hilo que lo espera.
 * Termina si recibir_mensaje devuelve NULL (conexión cerrada).
 */
void* km_listener_run(void* ctx_ptr);

#endif
