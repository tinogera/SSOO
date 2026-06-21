#include "ks_compact.h"
#include <utils/sockets.h>

void* km_listener_run(void* ctx_ptr) {
    t_km_listener_ctx* ctx = (t_km_listener_ctx*) ctx_ptr;

    while (1) {
        t_mensaje* msg = recibir_mensaje(ctx->fd_km);
        if (!msg) {
            log_error(ctx->logger, "## KM desconectado");
            break;
        }

        if (msg->op_code == MSG_COMPACTAR) {
            free_mensaje(msg);
            if (ctx->on_compactar) ctx->on_compactar();
        } else {
            pthread_mutex_lock(ctx->mutex_km_resp);
            *ctx->ultimo_resp_km_ptr = msg;
            pthread_cond_signal(ctx->cond_km_resp);
            pthread_mutex_unlock(ctx->mutex_km_resp);
        }
    }
    return NULL;
}
