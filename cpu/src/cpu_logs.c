#include "cpu_logs.h"

void log_cpu_fetch(t_log* logger, uint32_t pid, uint32_t pc) {
    log_info(logger, "## PID: %u - FETCH - Program Counter: %u", pid, pc);
}

void log_cpu_interrupcion(t_log* logger) {
    log_info(logger, "## Interrupcion recibida");
}

void log_cpu_ejecucion(t_log* logger, uint32_t pid, const char* instruccion, const char* parametros) {
    log_info(logger, "## PID: %u - Ejecutando: %s - %s", pid, instruccion, parametros);
}

void log_cpu_acceso_memoria(t_log* logger, uint32_t pid, const char* accion, uint32_t direccion_fisica, uint32_t valor) {
    log_info(
        logger,
        "PID: %u - Accion: %s - Direccion Fisica: %u - Valor: %u",
        pid,
        accion,
        direccion_fisica,
        valor
    );
}
