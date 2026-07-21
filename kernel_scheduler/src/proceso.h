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
    int      preemptado;        // 1 si fue desalojado por QUEUE_PREEMPTION (va al frente de READY)
    int      gen_despacho;      // se incrementa en cada despacho; invalida timers de quantum viejos
    int      gen_bloqueo;       // se incrementa en cada IO; invalida timers de suspensión viejos
    int      esperando_stdin;   // 1 hasta que los bytes de STDIN se persisten en KM
    time_t   tiempo_suspension; // epoch al momento de pasar a SUSP_BLOCK
} t_proceso;

#endif
