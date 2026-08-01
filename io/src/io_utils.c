#include "io_utils.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <errno.h>

int es_tipo_valido(const char* tipo) {
    return strcmp(tipo, "STDIN")  == 0 ||
           strcmp(tipo, "STDOUT") == 0 ||
           strcmp(tipo, "SLEEP")  == 0;
}

int handshake_exitoso(t_mensaje* respuesta) {
    return respuesta != NULL && respuesta->op_code == MSG_OK;
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

    t_payload_io_fin fin = { .pid = htonl(pid) };
    enviar_mensaje(fd_scheduler, MSG_IO_FIN, &fin, sizeof(fin));
}

// Devuelve el encabezado sin bytes. KS lo reconoce como respuesta incompleta
// (si se pidió al menos un byte) y finaliza el proceso de forma controlada en
// lugar de dejarlo bloqueado para siempre.
static void notificar_fallo_stdin(int fd_scheduler, uint32_t pid, uint32_t n_bytes) {
    uint32_t encabezado[2] = { htonl(pid), htonl(n_bytes) };
    enviar_mensaje(fd_scheduler, MSG_IO_STDIN_DATOS, encabezado, sizeof(encabezado));
}

void manejar_stdin(t_mensaje* msg, int fd_scheduler, t_log* logger) {
    if (!msg || msg->payload_size < sizeof(t_payload_io_stdin)) {
        log_error(logger, "STDIN recibió un pedido con payload inválido");
        if (msg) free_mensaje(msg);
        return;
    }

    t_payload_io_stdin* p = (t_payload_io_stdin*) msg->payload;
    uint32_t pid     = ntohl(p->pid);
    uint32_t n_bytes = ntohl(p->n_bytes);
    free_mensaje(msg);

    if (n_bytes > UINT32_MAX - 2u * sizeof(uint32_t)) {
        log_error(logger, "PID: %u - STDIN: tamaño inválido (%u bytes)", pid, n_bytes);
        notificar_fallo_stdin(fd_scheduler, pid, n_bytes);
        return;
    }

    log_info(logger, "## PID: %u - Inicio de IO", pid);
    log_info(logger, "## PID: %u - Ingrese %u caracteres:", pid, n_bytes);

    char* linea = calloc(n_bytes + 1, 1);
    if (!linea) {
        log_error(logger, "PID: %u - STDIN: sin memoria para leer los datos", pid);
        notificar_fallo_stdin(fd_scheduler, pid, n_bytes);
        return;
    }

    ssize_t leido;
    do {
        leido = read(STDIN_FILENO, linea, n_bytes + 1);
    } while (leido < 0 && errno == EINTR);

    log_debug(logger, "PID: %u - STDIN: %zd byte(s) leídos de %u pedidos (%s)",
              pid, leido, n_bytes,
              leido < 0 ? "error" : (leido > (ssize_t)n_bytes ? "truncado" : "rellenado con ceros si faltó"));
    if (leido < 0) {
        free(linea);
        log_error(logger, "PID: %u - STDIN: error leyendo la entrada", pid);
        notificar_fallo_stdin(fd_scheduler, pid, n_bytes);
        return;
    }
    if (leido > (ssize_t)n_bytes && linea[n_bytes] != '\n') {
        // Se escribió más de lo pedido. Consumir el resto de esta línea para
        // que no termine convertido en la entrada del siguiente proceso.
        char descartado;
        ssize_t r;
        do {
            do {
                r = read(STDIN_FILENO, &descartado, 1);
            } while (r < 0 && errno == EINTR);
        } while (r > 0 && descartado != '\n');
    }
    if (leido > 0 && linea[leido - 1] == '\n') linea[leido - 1] = '\0';
    linea[n_bytes] = '\0';

    log_info(logger, "## PID: %u - Fin de IO", pid);

    uint32_t payload_size = sizeof(pid) + sizeof(n_bytes) + n_bytes;
    void* payload_datos   = malloc(payload_size);
    if (!payload_datos) {
        free(linea);
        log_error(logger, "PID: %u - STDIN: sin memoria para devolver los datos", pid);
        notificar_fallo_stdin(fd_scheduler, pid, n_bytes);
        return;
    }
    uint32_t pid_n     = htonl(pid);
    uint32_t n_bytes_n = htonl(n_bytes);
    memcpy(payload_datos,                                  &pid_n,     sizeof(pid_n));
    memcpy((char*)payload_datos + sizeof(pid_n),           &n_bytes_n, sizeof(n_bytes_n));
    memcpy((char*)payload_datos + sizeof(pid) + sizeof(n_bytes), linea, n_bytes);
    free(linea);

    enviar_mensaje(fd_scheduler, MSG_IO_STDIN_DATOS, payload_datos, payload_size);
    free(payload_datos);
}

void manejar_stdout(t_mensaje* msg, int fd_scheduler, t_log* logger) {
    uint32_t pid_n;
    memcpy(&pid_n, msg->payload, sizeof(pid_n));
    uint32_t pid = ntohl(pid_n);

    size_t len = msg->payload_size - sizeof(pid);
    char* contenido = malloc(len + 1);
    memcpy(contenido, (char*)msg->payload + sizeof(pid), len);
    contenido[len] = '\0';
    free_mensaje(msg);

    log_info(logger, "## PID: %u - Inicio de IO", pid);
    log_info(logger, "## PID: %u - %s", pid, contenido);
    free(contenido);
    log_info(logger, "## PID: %u - Fin de IO", pid);

    t_payload_io_fin fin = { .pid = htonl(pid) };
    enviar_mensaje(fd_scheduler, MSG_IO_FIN, &fin, sizeof(fin));
}
