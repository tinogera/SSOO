#ifndef CPU_INTERRUPCIONES_H_
#define CPU_INTERRUPCIONES_H_

#include <stdint.h>
#include <commons/log.h>
#include <utils/protocolo.h>

typedef struct {
    uint32_t pid;
    t_motivo_interrupcion_cpu motivo;
} t_interrupcion_cpu;

typedef enum {
    CPU_INTERRUPCION_SIN_MENSAJE,
    CPU_INTERRUPCION_RECIBIDA,
    CPU_INTERRUPCION_ERROR
} t_resultado_interrupcion_cpu;

// La primera recibe el mensaje; la segunda usa select para consultar sin bloquear.
t_resultado_interrupcion_cpu recibir_interrupcion_cpu(
    int socket_kernel,
    uint32_t pid_en_ejecucion,
    t_interrupcion_cpu* interrupcion,
    t_log* logger
);
t_resultado_interrupcion_cpu recibir_interrupcion_cpu_si_hay(
    int socket_kernel,
    uint32_t pid_en_ejecucion,
    t_interrupcion_cpu* interrupcion,
    t_log* logger
);
const char* motivo_interrupcion_to_string(t_motivo_interrupcion_cpu motivo);

#endif
