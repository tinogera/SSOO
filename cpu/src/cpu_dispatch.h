#ifndef CPU_DISPATCH_H_
#define CPU_DISPATCH_H_

#include <stdbool.h>
#include <stdint.h>
#include <commons/log.h>

bool recibir_proceso_a_ejecutar(int socket_kernel, uint32_t* pid, t_log* logger);

#endif
