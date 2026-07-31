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

/*
 * Acá está el corazón de CPU: fetch, decode y execute en un while.
 * El while no termina porque se acabó un quantum por sí solo. Termina cuando
 * aparece una syscall, EXIT, una interrupción de KS, un segfault o un error.
 */

// Armo de nuevo los parámetros únicamente para dejar el log legible.
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

// Esta función no resuelve la syscall: sólo valida parámetros, arma el payload
// y se la comunica a KS, que es quien decide si el proceso bloquea o continúa.
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

            // En STDIN/STDOUT los parámetros son nombres de registros. Sus
            // valores contienen la dirección lógica y la cantidad de bytes.
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
    t_cpu_memory_sticks* memory_sticks,
    uint32_t pid,
    t_contexto* contexto,
    t_registros_cpu* registros,
    t_log* logger
) {
    while (true) {
        // FETCH: KM tiene los scripts, por eso le pido la línea indicada por PC.
        char* instruccion_texto = fetch_instruccion(socket_memory, pid, registros, logger);
        if (instruccion_texto == NULL) {
            return CPU_CICLO_ERROR_FETCH;
        }

        // DECODE: paso del texto a un opcode y un arreglo de parámetros.
        t_instruccion_decodificada instruccion = decode_instruccion(instruccion_texto);
        free(instruccion_texto);

        if (instruccion.opcode == CPU_INST_UNKNOWN) {
            log_error(logger, "No se pudo decodificar la instruccion del PID %u", pid);
            return CPU_CICLO_ERROR_DECODE;
        }

        if (es_syscall_o_exit(instruccion.opcode)) {
            // Avanzo el PC antes de devolver el proceso para que, al reanudarlo,
            // no vuelva a ejecutar la misma syscall.
            registros->pc++;

            if (!ejecutar_syscall(socket_kernel, pid, &instruccion, registros, logger)) {
                return CPU_CICLO_ERROR_EXECUTE;
            }

            return instruccion.opcode == CPU_INST_EXIT ? CPU_CICLO_EXIT : CPU_CICLO_SYSCALL;
        }

        if (es_instruccion_memoria(instruccion.opcode)) {
            // MOV_IN, MOV_OUT y COPY_MEM hacen MMU y después hablan con el MS.
            t_resultado_memoria_cpu resultado_memoria = ejecutar_instruccion_memoria(
                memory_sticks,
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

            // El desalojo se revisa entre instrucciones, nunca a mitad de una.
            // De esta manera el contexto queda en un punto consistente.
            t_interrupcion_cpu interrupcion;
            t_resultado_interrupcion_cpu resultado_interrupcion =
                recibir_interrupcion_cpu_si_hay(socket_kernel, pid, &interrupcion, logger);
            if (resultado_interrupcion == CPU_INTERRUPCION_RECIBIDA) {
                return CPU_CICLO_INTERRUPCION;
            }
            if (resultado_interrupcion == CPU_INTERRUPCION_ERROR) {
                return CPU_CICLO_ERROR_EXECUTE;
            }

            continue;
        }

        // Si no era syscall ni memoria, es una instrucción básica de registros.
        t_resultado_ejecucion resultado = ejecutar_instruccion(&instruccion, registros, pid, logger);
        if (resultado != CPU_EXEC_OK) {
            log_error(logger, "No se pudo ejecutar la instruccion del PID %u", pid);
            return CPU_CICLO_ERROR_EXECUTE;
        }

        // También reviso interrupciones después de cada instrucción básica.
        t_interrupcion_cpu interrupcion;
        t_resultado_interrupcion_cpu resultado_interrupcion =
            recibir_interrupcion_cpu_si_hay(socket_kernel, pid, &interrupcion, logger);
        if (resultado_interrupcion == CPU_INTERRUPCION_RECIBIDA) {
            return CPU_CICLO_INTERRUPCION;
        }
        if (resultado_interrupcion == CPU_INTERRUPCION_ERROR) {
            return CPU_CICLO_ERROR_EXECUTE;
        }
    }
}
