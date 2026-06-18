#include "cpu_decode.h"

#include <string.h>

static t_opcode_cpu obtener_opcode(const char* nombre) {
    if (strcmp(nombre, "NOOP") == 0) return CPU_INST_NOOP;
    if (strcmp(nombre, "SET") == 0) return CPU_INST_SET;
    if (strcmp(nombre, "SUM") == 0) return CPU_INST_SUM;
    if (strcmp(nombre, "SUB") == 0) return CPU_INST_SUB;
    if (strcmp(nombre, "JNZ") == 0) return CPU_INST_JNZ;
    if (strcmp(nombre, "MUTEX_CREATE") == 0) return CPU_INST_MUTEX_CREATE;
    if (strcmp(nombre, "MUTEX_LOCK") == 0) return CPU_INST_MUTEX_LOCK;
    if (strcmp(nombre, "MUTEX_UNLOCK") == 0) return CPU_INST_MUTEX_UNLOCK;
    if (strcmp(nombre, "SLEEP") == 0) return CPU_INST_SLEEP;
    if (strcmp(nombre, "STDOUT") == 0) return CPU_INST_STDOUT;
    if (strcmp(nombre, "STDIN") == 0) return CPU_INST_STDIN;
    if (strcmp(nombre, "EXIT") == 0) return CPU_INST_EXIT;
    if (strcmp(nombre, "MOV_IN") == 0) return CPU_INST_MOV_IN;
    if (strcmp(nombre, "MOV_OUT") == 0) return CPU_INST_MOV_OUT;
    if (strcmp(nombre, "MEM_ALLOC") == 0) return CPU_INST_MEM_ALLOC;
    if (strcmp(nombre, "MEM_FREE") == 0) return CPU_INST_MEM_FREE;
    
    return CPU_INST_UNKNOWN;
}

t_instruccion_decodificada decode_instruccion(const char* instruccion) {
    t_instruccion_decodificada resultado = {
        .opcode = CPU_INST_UNKNOWN,
        .cantidad_parametros = 0
    };

    if (instruccion == NULL) {
        return resultado;
    }

    char buffer[256];
    strncpy(buffer, instruccion, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';

    char* token = strtok(buffer, " \t\r\n");
    if (token == NULL) {
        return resultado;
    }

    resultado.opcode = obtener_opcode(token);

    while (resultado.cantidad_parametros < CPU_MAX_PARAMETROS) {
        token = strtok(NULL, " \t\r\n");
        if (token == NULL) {
            break;
        }

        strncpy(
            resultado.parametros[resultado.cantidad_parametros],
            token,
            CPU_MAX_PARAMETRO_LENGTH - 1
        );
        resultado.parametros[resultado.cantidad_parametros][CPU_MAX_PARAMETRO_LENGTH - 1] = '\0';
        resultado.cantidad_parametros++;
    }

    return resultado;
}

const char* opcode_cpu_to_string(t_opcode_cpu opcode) {
    switch (opcode) {
        case CPU_INST_NOOP: return "NOOP";
        case CPU_INST_SET: return "SET";
        case CPU_INST_SUM: return "SUM";
        case CPU_INST_SUB: return "SUB";
        case CPU_INST_JNZ: return "JNZ";
        case CPU_INST_MUTEX_CREATE: return "MUTEX_CREATE";
        case CPU_INST_MUTEX_LOCK: return "MUTEX_LOCK";
        case CPU_INST_MUTEX_UNLOCK: return "MUTEX_UNLOCK";
        case CPU_INST_SLEEP: return "SLEEP";
        case CPU_INST_STDOUT: return "STDOUT";
        case CPU_INST_STDIN: return "STDIN";
        case CPU_INST_EXIT: return "EXIT";
        default: return "UNKNOWN";
    }
}
