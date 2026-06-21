#include "cpu_ciclo.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "cpu_decode.h"
#include "cpu_execute.h"
#include "cpu_fetch.h"
#include "cpu_interrupciones.h"
#include "cpu_logs.h"
#include "cpu_memoria.h"
#include "cpu_syscalls.h"
#include "cpu_contexto.h"
#include "cpu_mmu.h"

static void armar_parametros(t_instruccion_decodificada* instruccion, char* parametros, size_t parametros_size) {
    parametros[0] = '\0';

    for (int i = 0; i < instruccion->cantidad_parametros; i++) {
        if (i > 0) {
            strncat(parametros, " ", parametros_size - strlen(parametros) - 1);
        }
        strncat(parametros, instruccion->parametros[i], parametros_size - strlen(parametros) - 1);
    }
}

static uint32_t parsear_uint32(const char* valor) {
    return (uint32_t) strtoul(valor, NULL, 10);
}

static bool ejecutar_syscall(
    int socket_kernel,
    uint32_t pid,
    t_instruccion_decodificada* instruccion,
    t_registros_cpu* registros,
    t_log* logger
) {
    char parametros[CPU_MAX_PARAMETROS * CPU_MAX_PARAMETRO_LENGTH];
    armar_parametros(instruccion, parametros, sizeof(parametros));
    log_cpu_ejecucion(logger, pid, opcode_cpu_to_string(instruccion->opcode), parametros);

    switch (instruccion->opcode) {
        case CPU_INST_MUTEX_CREATE:
            return instruccion->cantidad_parametros == 1 &&
                   enviar_syscall_mutex_create(socket_kernel, pid, instruccion->parametros[0], logger);
        case CPU_INST_MUTEX_LOCK:
            return instruccion->cantidad_parametros == 1 &&
                   enviar_syscall_mutex_lock(socket_kernel, pid, instruccion->parametros[0], logger);
        case CPU_INST_MUTEX_UNLOCK:
            return instruccion->cantidad_parametros == 1 &&
                   enviar_syscall_mutex_unlock(socket_kernel, pid, instruccion->parametros[0], logger);
        case CPU_INST_SLEEP:
            return instruccion->cantidad_parametros == 1 &&
                   enviar_syscall_sleep(socket_kernel, pid, parsear_uint32(instruccion->parametros[0]), logger);
        case CPU_INST_MEM_ALLOC:
            return instruccion->cantidad_parametros == 2 &&
                   enviar_syscall_mem_alloc(
                       socket_kernel,
                       pid,
                       parsear_uint32(instruccion->parametros[0]),
                       parsear_uint32(instruccion->parametros[1]),
                       logger
                   );
        case CPU_INST_MEM_FREE:
            return instruccion->cantidad_parametros == 1 &&
                   enviar_syscall_mem_free(
                       socket_kernel,
                       pid,
                       parsear_uint32(instruccion->parametros[0]),
                       logger
                   );
        case CPU_INST_INIT_PROC:
            return instruccion->cantidad_parametros == 2 &&
                   enviar_syscall_init_proc(
                       socket_kernel,
                       pid,
                       instruccion->parametros[0],
                       parsear_uint32(instruccion->parametros[1]),
                       logger
                   );
        case CPU_INST_STDOUT:
        case CPU_INST_STDIN: {
            if (instruccion->cantidad_parametros != 2) {
                return false;
            }

            uint32_t direccion_logica;
            uint32_t tamanio;
            if (
                !leer_valor_registro_cpu(registros, instruccion->parametros[0], &direccion_logica) ||
                !leer_valor_registro_cpu(registros, instruccion->parametros[1], &tamanio)
            ) {
                return false;
            }

            if (instruccion->opcode == CPU_INST_STDOUT) {
                return enviar_syscall_stdout(socket_kernel, pid, direccion_logica, tamanio, logger);
            }

            return enviar_syscall_stdin(socket_kernel, pid, direccion_logica, tamanio, logger);
        }
        case CPU_INST_EXIT:
            return instruccion->cantidad_parametros == 0 &&
                   enviar_syscall_exit(socket_kernel, pid, logger);
        default:
            return false;
    }
}

static bool es_syscall_o_exit(t_opcode_cpu opcode) {
    return opcode == CPU_INST_MUTEX_CREATE ||
           opcode == CPU_INST_MUTEX_LOCK ||
           opcode == CPU_INST_MUTEX_UNLOCK ||
           opcode == CPU_INST_SLEEP ||
           opcode == CPU_INST_MEM_ALLOC ||
           opcode == CPU_INST_MEM_FREE ||
           opcode == CPU_INST_INIT_PROC ||
           opcode == CPU_INST_STDOUT ||
           opcode == CPU_INST_STDIN ||
           opcode == CPU_INST_EXIT;
}

static bool es_instruccion_memoria(t_opcode_cpu opcode) {
    return opcode == CPU_INST_MOV_IN ||
           opcode == CPU_INST_MOV_OUT ||
           opcode == CPU_INST_COPY_MEM;
}

t_resultado_ciclo_cpu ejecutar_ciclo_proceso(
    int socket_kernel,
    int socket_memory,
    int socket_memoria_usuario,
    uint32_t pid,
    t_contexto* contexto,
    t_registros_cpu* registros,
    t_log* logger
) {
    while (true) {
        char* instruccion_texto = fetch_instruccion(socket_memory, pid, registros, logger);
        if (instruccion_texto == NULL) {
            return CPU_CICLO_ERROR_FETCH;
        }

        t_instruccion_decodificada instruccion = decode_instruccion(instruccion_texto);
        free(instruccion_texto);

        if (instruccion.opcode == CPU_INST_UNKNOWN) {
            log_error(logger, "No se pudo decodificar la instruccion del PID %u", pid);
            return CPU_CICLO_ERROR_DECODE;
        }

        if (es_syscall_o_exit(instruccion.opcode)) {
            registros->pc++;

            if (!ejecutar_syscall(socket_kernel, pid, &instruccion, registros, logger)) {
                return CPU_CICLO_ERROR_EXECUTE;
            }

            return instruccion.opcode == CPU_INST_EXIT ? CPU_CICLO_EXIT : CPU_CICLO_SYSCALL;
        }

        if (es_instruccion_memoria(instruccion.opcode)) {
            t_resultado_memoria_cpu resultado_memoria = ejecutar_instruccion_memoria(
                socket_memoria_usuario,
                &instruccion,
                contexto,
                registros,
                pid,
                logger
            );

            if (resultado_memoria == CPU_MEMORIA_SEG_FAULT) {
                return CPU_CICLO_SEG_FAULT;
            }

            if (resultado_memoria != CPU_MEMORIA_OK) {
                log_error(logger, "No se pudo ejecutar la instruccion de memoria del PID %u", pid);
                return CPU_CICLO_ERROR_EXECUTE;
            }

            t_interrupcion_cpu interrupcion;
            if (recibir_interrupcion_cpu_si_hay(socket_kernel, &interrupcion, logger)) {
                return CPU_CICLO_INTERRUPCION;
            }

            continue;
        }

        t_resultado_ejecucion resultado = ejecutar_instruccion(&instruccion, registros, pid, logger);
        if (resultado != CPU_EXEC_OK) {
            log_error(logger, "No se pudo ejecutar la instruccion del PID %u", pid);
            return CPU_CICLO_ERROR_EXECUTE;
        }

        t_interrupcion_cpu interrupcion;
        if (recibir_interrupcion_cpu_si_hay(socket_kernel, &interrupcion, logger)) {
            return CPU_CICLO_INTERRUPCION;
        }
    }
}
