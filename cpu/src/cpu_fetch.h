#ifndef CPU_FETCH_H_
#define CPU_FETCH_H_

#include <stdint.h>
#include <commons/log.h>

#include "cpu_registros.h"

char* fetch_instruccion(int socket_memory, uint32_t pid, t_registros_cpu* registros, t_log* logger);

#endif
