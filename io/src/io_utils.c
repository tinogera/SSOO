#include "io_utils.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

int es_tipo_valido(const char* tipo) {
    return strcmp(tipo, "STDIN")  == 0 ||
           strcmp(tipo, "STDOUT") == 0 ||
           strcmp(tipo, "SLEEP")  == 0;
}

void manejar_sleep(t_mensaje* msg, int fd_scheduler, t_log* logger) {
    t_payload_io_sleep* p = (t_payload_io_sleep*) msg->payload;
    uint32_t pid       = ntohl(p->pid);
    uint32_t tiempo_ms = ntohl(p->tiempo_ms);
    free_mensaje(msg);

    log_info(logger, "## PID: %u - Inicio de IO", pid);
    log_info(logger, "## PID: %u - Haciendo sleep por %u milisegundos", pid, tiempo_ms);

    usleep((useconds_t)tiempo_ms * 1000);

    log_info(logger, "## PID: %u - Fin de IO", pid);

    t_payload_io_fin fin = { .pid = pid };
    enviar_mensaje(fd_scheduler, MSG_IO_FIN, &fin, sizeof(fin));
}

void manejar_stdin(t_mensaje* msg, int fd_scheduler, t_log* logger) {
    t_payload_io_stdin* p = (t_payload_io_stdin*) msg->payload;
    uint32_t pid     = ntohl(p->pid);
    uint32_t n_bytes = ntohl(p->n_bytes);
    free_mensaje(msg);

    log_info(logger, "## PID: %u - Inicio de IO", pid);
    log_info(logger, "## PID: %u - Ingrese %u caracteres:", pid, n_bytes);

    char* linea = calloc(n_bytes + 1, 1);
    ssize_t leido = read(STDIN_FILENO, linea, n_bytes + 1);
    if (leido > 0 && linea[leido - 1] == '\n') linea[leido - 1] = '\0';
    linea[n_bytes] = '\0';

    log_info(logger, "## PID: %u - Fin de IO", pid);

    uint32_t payload_size = sizeof(pid) + sizeof(n_bytes) + n_bytes;
    void* payload_datos   = malloc(payload_size);
    memcpy(payload_datos,                                  &pid,     sizeof(pid));
    memcpy((char*)payload_datos + sizeof(pid),             &n_bytes, sizeof(n_bytes));
    memcpy((char*)payload_datos + sizeof(pid) + sizeof(n_bytes), linea, n_bytes);
    free(linea);

    enviar_mensaje(fd_scheduler, MSG_IO_STDIN_DATOS, payload_datos, payload_size);
    free(payload_datos);
}

void manejar_stdout(t_mensaje* msg, int fd_scheduler, t_log* logger) {
    uint32_t pid;
    memcpy(&pid, msg->payload, sizeof(pid));

    size_t len = msg->payload_size - sizeof(pid);
    char* contenido = malloc(len + 1);
    memcpy(contenido, (char*)msg->payload + sizeof(pid), len);
    contenido[len] = '\0';
    free_mensaje(msg);

    log_info(logger, "## PID: %u - Inicio de IO", pid);
    log_info(logger, "## PID: %u - %s", pid, contenido);
    free(contenido);
    log_info(logger, "## PID: %u - Fin de IO", pid);

    t_payload_io_fin fin = { .pid = pid };
    enviar_mensaje(fd_scheduler, MSG_IO_FIN, &fin, sizeof(fin));
}
