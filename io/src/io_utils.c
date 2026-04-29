#include "io_utils.h"
#include <string.h>
#include <unistd.h>

int es_tipo_valido(const char* tipo) {
    return strcmp(tipo, "STDIN")  == 0 ||
           strcmp(tipo, "STDOUT") == 0 ||
           strcmp(tipo, "SLEEP")  == 0;
}

void manejar_sleep(t_mensaje* msg, int fd_scheduler, t_log* logger) {
    t_payload_io_sleep* p = (t_payload_io_sleep*) msg->payload;
    uint32_t pid       = p->pid;
    uint32_t tiempo_ms = p->tiempo_ms;
    free_mensaje(msg);

    log_info(logger, "## PID: %u - Inicio de IO", pid);
    log_info(logger, "## PID: %u - Haciendo sleep por %u milisegundos", pid, tiempo_ms);

    usleep((useconds_t)tiempo_ms * 1000);

    log_info(logger, "## PID: %u - Fin de IO", pid);

    t_payload_io_fin fin = { .pid = pid };
    enviar_mensaje(fd_scheduler, MSG_IO_FIN, &fin, sizeof(fin));
}
