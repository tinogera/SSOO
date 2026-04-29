#ifndef IO_UTILS_H_
#define IO_UTILS_H_

#include <commons/log.h>
#include <utils/sockets.h>
#include <utils/protocolo.h>

int  es_tipo_valido(const char* tipo);

void manejar_sleep(t_mensaje* msg, int fd_scheduler, t_log* logger);

#endif
