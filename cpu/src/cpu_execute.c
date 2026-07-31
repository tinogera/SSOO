#include "cpu_execute.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "cpu_logs.h"
#include "cpu_mmu.h"

/*
 * En este archivo ejecuto sólo las instrucciones básicas. No hay sockets ni
 * decisiones de planificación: leo/escribo registros y dejo actualizado el PC.
 */
static uint32_t parsear_uint32(const char* valor) {
    return (uint32_t) strtoul(valor, NULL, 10);
}

static void avanzar_pc_si_corresponde(t_registros_cpu* registros, const char* registro_destino) {
    if (strcmp(registro_destino, "PC") != 0) {
        registros->pc++;
    }
}

static void loguear_ejecucion(t_instruccion_decodificada* instruccion, uint32_t pid, t_log* logger) {
    char parametros[CPU_MAX_PARAMETROS * CPU_MAX_PARAMETRO_LENGTH] = "";

    for (int i = 0; i < instruccion->cantidad_parametros; i++) {
        if (i > 0) {
            strncat(parametros, " ", sizeof(parametros) - strlen(parametros) - 1);
        }
        strncat(parametros, instruccion->parametros[i], sizeof(parametros) - strlen(parametros) - 1);
    }

    log_cpu_ejecucion(logger, pid, opcode_cpu_to_string(instruccion->opcode), parametros);
}

static t_resultado_ejecucion ejecutar_noop(t_registros_cpu* registros) {
    // NOOP no cambia datos, pero sí consume una instrucción.
    registros->pc++;
    return CPU_EXEC_OK;
}

static t_resultado_ejecucion ejecutar_set(t_instruccion_decodificada* instruccion, t_registros_cpu* registros) {
    if (instruccion->cantidad_parametros != 2) {
        return CPU_EXEC_ERROR;
    }

    if (!escribir_valor_registro_cpu(registros, instruccion->parametros[0], parsear_uint32(instruccion->parametros[1]))) {
        return CPU_EXEC_ERROR;
    }

    // Si la instrucción escribió PC, ese valor ya indica la próxima instrucción.
    avanzar_pc_si_corresponde(registros, instruccion->parametros[0]);
    return CPU_EXEC_OK;
}

static t_resultado_ejecucion ejecutar_sum(t_instruccion_decodificada* instruccion, t_registros_cpu* registros) {
    if (instruccion->cantidad_parametros != 2) {
        return CPU_EXEC_ERROR;
    }

    uint32_t destino;
    uint32_t origen;
    if (
        !leer_valor_registro_cpu(registros, instruccion->parametros[0], &destino) ||
        !leer_valor_registro_cpu(registros, instruccion->parametros[1], &origen)
    ) {
        return CPU_EXEC_ERROR;
    }

    // El primer registro es destino y también uno de los operandos.
    if (!escribir_valor_registro_cpu(registros, instruccion->parametros[0], destino + origen)) {
        return CPU_EXEC_ERROR;
    }

    avanzar_pc_si_corresponde(registros, instruccion->parametros[0]);
    return CPU_EXEC_OK;
}

static t_resultado_ejecucion ejecutar_sub(t_instruccion_decodificada* instruccion, t_registros_cpu* registros) {
    if (instruccion->cantidad_parametros != 2) {
        return CPU_EXEC_ERROR;
    }

    uint32_t destino;
    uint32_t origen;
    if (
        !leer_valor_registro_cpu(registros, instruccion->parametros[0], &destino) ||
        !leer_valor_registro_cpu(registros, instruccion->parametros[1], &origen)
    ) {
        return CPU_EXEC_ERROR;
    }

    // SUB conserva el orden: destino = destino - origen.
    if (!escribir_valor_registro_cpu(registros, instruccion->parametros[0], destino - origen)) {
        return CPU_EXEC_ERROR;
    }

    avanzar_pc_si_corresponde(registros, instruccion->parametros[0]);
    return CPU_EXEC_OK;
}

static t_resultado_ejecucion ejecutar_jnz(t_instruccion_decodificada* instruccion, t_registros_cpu* registros) {
    if (instruccion->cantidad_parametros != 2) {
        return CPU_EXEC_ERROR;
    }

    uint32_t valor_registro;
    if (!leer_valor_registro_cpu(registros, instruccion->parametros[0], &valor_registro)) {
        return CPU_EXEC_ERROR;
    }

    // Si hay salto, el destino reemplaza PC; si no, continúa normalmente.
    if (valor_registro != 0) {
        registros->pc = parsear_uint32(instruccion->parametros[1]);
    } else {
        registros->pc++;
    }

    return CPU_EXEC_OK;
}

t_resultado_ejecucion ejecutar_instruccion(
    t_instruccion_decodificada* instruccion,
    t_registros_cpu* registros,
    uint32_t pid,
    t_log* logger
) {
    if (instruccion == NULL || registros == NULL) {
        return CPU_EXEC_ERROR;
    }

    loguear_ejecucion(instruccion, pid, logger);

    // Este switch es el execute de las instrucciones que sólo usan registros.
    switch (instruccion->opcode) {
        case CPU_INST_NOOP:
            return ejecutar_noop(registros);
        case CPU_INST_SET:
            return ejecutar_set(instruccion, registros);
        case CPU_INST_SUM:
            return ejecutar_sum(instruccion, registros);
        case CPU_INST_SUB:
            return ejecutar_sub(instruccion, registros);
        case CPU_INST_JNZ:
            return ejecutar_jnz(instruccion, registros);
        default:
            return CPU_EXEC_ERROR;
    }
}
