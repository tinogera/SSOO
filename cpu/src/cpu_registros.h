#ifndef CPU_REGISTROS_H_
#define CPU_REGISTROS_H_

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t pc;

    uint8_t ax;
    uint8_t bx;
    uint8_t cx;
    uint8_t dx;

    uint32_t eax;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;

    uint32_t si;
    uint32_t di;
} t_registros_cpu;

void inicializar_registros_cpu(t_registros_cpu* registros);
bool leer_valor_registro_cpu(t_registros_cpu* registros, const char* nombre, uint32_t* valor);

#endif
