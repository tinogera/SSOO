#ifndef KS_PROCESO_H_
#define KS_PROCESO_H_

#include <stdint.h>
#include <time.h>

typedef enum {
    NEW, READY, EXEC, BLOCK, SUSP_BLOCK, SUSP_READY, EXIT
} t_estado;

typedef struct {
    int      PID;
    t_estado estado;
    uint32_t controladorDeProgramas;
    int      prioridad;
    int      fd_cpu;            // fd de la CPU que lo ejecuta (-1 si ninguna)
    time_t   tiempo_suspension; // epoch al momento de pasar a SUSP_BLOCK
} t_proceso;

#endif
