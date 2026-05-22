#ifndef CPU_DECODE_H_
#define CPU_DECODE_H_

#define CPU_MAX_PARAMETROS 3
#define CPU_MAX_PARAMETRO_LENGTH 64

typedef enum {
    CPU_INST_NOOP,
    CPU_INST_SET,
    CPU_INST_SUM,
    CPU_INST_SUB,
    CPU_INST_JNZ,
    CPU_INST_MUTEX_CREATE,
    CPU_INST_MUTEX_LOCK,
    CPU_INST_MUTEX_UNLOCK,
    CPU_INST_SLEEP,
    CPU_INST_STDOUT,
    CPU_INST_STDIN,
    CPU_INST_EXIT,
    CPU_INST_UNKNOWN
} t_opcode_cpu;

typedef struct {
    t_opcode_cpu opcode;
    int cantidad_parametros;
    char parametros[CPU_MAX_PARAMETROS][CPU_MAX_PARAMETRO_LENGTH];
} t_instruccion_decodificada;

t_instruccion_decodificada decode_instruccion(const char* instruccion);
const char* opcode_cpu_to_string(t_opcode_cpu opcode);

#endif
