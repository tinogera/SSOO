#ifndef CPU_MEMORIA_H_
#define CPU_MEMORIA_H_

#include <stdbool.h>
#include <stdint.h>

bool memoria_write(int socket_ms, uint32_t direccion, uint32_t tamanio, void* datos);
bool memoria_read(int socket_ms, uint32_t direccion, uint32_t tamanio, void* destino);

#endif