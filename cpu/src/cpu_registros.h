#ifndef CPU_REGISTROS_H_
#define CPU_REGISTROS_H_

#include <stdbool.h>
#include <stdint.h>
#include <utils/sockets.h>

void inicializar_registros_cpu(t_registros_cpu* registros);
bool leer_valor_registro_cpu(t_registros_cpu* registros, const char* nombre, uint32_t* valor);
bool escribir_valor_registro_cpu(t_registros_cpu* registros, const char* nombre, uint32_t valor);
bool tamanio_registro_cpu(const char* nombre, uint32_t* tamanio);

#endif
