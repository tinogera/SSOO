#ifndef CPU_LOGS_H_
#define CPU_LOGS_H_

#include <stdint.h>
#include <commons/log.h>

void log_cpu_fetch(t_log* logger, uint32_t pid, uint32_t pc);
void log_cpu_interrupcion(t_log* logger);
void log_cpu_ejecucion(t_log* logger, uint32_t pid, const char* instruccion, const char* parametros);
void log_cpu_acceso_memoria(t_log* logger, uint32_t pid, const char* accion, uint32_t direccion_fisica, uint32_t valor);

#endif
