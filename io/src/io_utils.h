#ifndef IO_UTILS_H_
#define IO_UTILS_H_

#include <commons/log.h>
#include <utils/sockets.h>
#include <utils/protocolo.h>

int  es_tipo_valido(const char* tipo);

// Evalúa la respuesta al handshake de identificación ante el Kernel Scheduler.
// respuesta puede ser NULL (el KS cerró la conexión antes de responder).
// Devuelve 1 solo si respuesta != NULL y respuesta->op_code == MSG_OK.
int  handshake_exitoso(t_mensaje* respuesta);

void manejar_sleep (t_mensaje* msg, int fd_scheduler, t_log* logger);
void manejar_stdout(t_mensaje* msg, int fd_scheduler, t_log* logger);
void manejar_stdin (t_mensaje* msg, int fd_scheduler, t_log* logger);

#endif
