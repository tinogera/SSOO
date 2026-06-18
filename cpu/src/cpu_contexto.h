#ifndef CPU_CONTEXTO_H_
#define CPU_CONTEXTO_H_

#include <stdbool.h>
#include <stdint.h>
#include <commons/log.h>

#include "cpu_registros.h"

bool restaurar_contexto_desde_memory(int socket_memory, uint32_t pid, t_registros_cpu* registros, t_log* logger);
bool guardar_contexto_en_memory(int socket_memory, uint32_t pid, t_registros_cpu* registros, t_log* logger);

//lo necesito para la tabla de segmentos
extern t_contexto* contexto_actual;
#endif
