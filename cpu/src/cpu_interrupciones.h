#ifndef CPU_INTERRUPCIONES_H_
#define CPU_INTERRUPCIONES_H_

#include <stdbool.h>
#include <stdint.h>
#include <commons/log.h>
#include <utils/protocolo.h>

typedef struct {
    // KS informa el PID afectado y si fue quantum o desalojo por prioridad.
    uint32_t pid;
    t_motivo_interrupcion_cpu motivo;
} t_interrupcion_cpu;

// La primera recibe el mensaje; la segunda usa select para consultar sin bloquear.
bool recibir_interrupcion_cpu(int socket_kernel, t_interrupcion_cpu* interrupcion, t_log* logger);
bool recibir_interrupcion_cpu_si_hay(int socket_kernel, t_interrupcion_cpu* interrupcion, t_log* logger);
const char* motivo_interrupcion_to_string(t_motivo_interrupcion_cpu motivo);

#endif
