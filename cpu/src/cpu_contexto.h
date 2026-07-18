#ifndef CPU_CONTEXTO_H_
#define CPU_CONTEXTO_H_

#include <stdbool.h>
#include <stdint.h>
#include <commons/log.h>

#include "cpu_registros.h"

// Restaurar trae una copia desde KM; guardar persiste los cambios de la ráfaga.
bool restaurar_contexto_desde_memory(int socket_memory, uint32_t pid, t_contexto** contexto, t_registros_cpu* registros, t_log* logger);
bool guardar_contexto_en_memory(int socket_memory, t_contexto* contexto, t_registros_cpu* registros, t_log* logger);
void liberar_contexto_cpu(t_contexto* contexto);

// Está declarado pero no tiene definición ni uso actual. Lo dejo identificado
// para no confundirlo con el contexto local que maneja main durante una ráfaga.
extern t_contexto* contexto_actual;
#endif
