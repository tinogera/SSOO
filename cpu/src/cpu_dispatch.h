#ifndef CPU_DISPATCH_H_
#define CPU_DISPATCH_H_

#include <stdbool.h>
#include <stdint.h>
#include <commons/log.h>

// Queda esperando hasta recibir un MSG_DESPACHAR_PROCESO válido.
bool recibir_proceso_a_ejecutar(int socket_kernel, uint32_t* pid, t_log* logger);

#endif
