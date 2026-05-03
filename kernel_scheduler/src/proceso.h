#ifndef KS_PROCESO_H_
#define KS_PROCESO_H_

typedef enum { 
    // ---------------
    // ESTADOS DE TAREAS
    // -----------------
    NEW, 
    READY,
    EXEC, 
    BLOCK, 
    SUSP_BLOCK, 
    SUSP_READY, 
    EXIT 
} t_estado;

typedef struct {
    int PID;
    t_estado estado;
    uint32_t controladorDeProgramas ;//apunta a la sig estruccion
    int prioridad;
    // registros de CPU — Kevin
    // memoria — Luciano
} t_proceso;

#endif