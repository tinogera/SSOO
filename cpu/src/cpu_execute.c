#include "cpu_execute.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "cpu_logs.h"
#include "cpu_mmu.h"

<<<<<<< HEAD
=======
typedef enum {
    REGISTRO_8_BITS,
    REGISTRO_32_BITS
} t_tamanio_registro;

static bool leer_registro(t_registros_cpu* registros, const char* nombre, uint32_t* valor) {
    if (strcmp(nombre, "AX") == 0)  { *valor = registros->ax;  return true; }
    if (strcmp(nombre, "BX") == 0)  { *valor = registros->bx;  return true; }
    if (strcmp(nombre, "CX") == 0)  { *valor = registros->cx;  return true; }
    if (strcmp(nombre, "DX") == 0)  { *valor = registros->dx;  return true; }
    if (strcmp(nombre, "EAX") == 0) { *valor = registros->eax; return true; }
    if (strcmp(nombre, "EBX") == 0) { *valor = registros->ebx; return true; }
    if (strcmp(nombre, "ECX") == 0) { *valor = registros->ecx; return true; }
    if (strcmp(nombre, "EDX") == 0) { *valor = registros->edx; return true; }
    if (strcmp(nombre, "SI") == 0)  { *valor = registros->si;  return true; }
    if (strcmp(nombre, "DI") == 0)  { *valor = registros->di;  return true; }
    if (strcmp(nombre, "PC") == 0)  { *valor = registros->pc;  return true; }
    return false;
}

static bool tamanio_registro(const char* nombre, t_tamanio_registro* tamanio) {
    if (
        strcmp(nombre, "AX") == 0 ||
        strcmp(nombre, "BX") == 0 ||
        strcmp(nombre, "CX") == 0 ||
        strcmp(nombre, "DX") == 0
    ) {
        *tamanio = REGISTRO_8_BITS;
        return true;
    }

    if (
        strcmp(nombre, "EAX") == 0 ||
        strcmp(nombre, "EBX") == 0 ||
        strcmp(nombre, "ECX") == 0 ||
        strcmp(nombre, "EDX") == 0 ||
        strcmp(nombre, "SI") == 0 ||
        strcmp(nombre, "DI") == 0 ||
        strcmp(nombre, "PC") == 0
    ) {
        *tamanio = REGISTRO_32_BITS;
        return true;
    }

    return false;
}

static uint32_t obtener_bytes_registro( const char* nombre ){
    t_tamanio_registro tipo;

    if(!tamanio_registro(nombre, &tipo))
    {
        return 0;
    }

    if(tipo == REGISTRO_8_BITS)
    {
        return 1;
    }

    return 4;
}

static bool escribir_registro(t_registros_cpu* registros, const char* nombre, uint32_t valor) {
    t_tamanio_registro tamanio;
    if (!tamanio_registro(nombre, &tamanio)) {
        return false;
    }

    if (tamanio == REGISTRO_8_BITS) {
        uint8_t valor_8 = (uint8_t) valor;

        if (strcmp(nombre, "AX") == 0) { registros->ax = valor_8; return true; }
        if (strcmp(nombre, "BX") == 0) { registros->bx = valor_8; return true; }
        if (strcmp(nombre, "CX") == 0) { registros->cx = valor_8; return true; }
        if (strcmp(nombre, "DX") == 0) { registros->dx = valor_8; return true; }
    }

    if (strcmp(nombre, "EAX") == 0) { registros->eax = valor; return true; }
    if (strcmp(nombre, "EBX") == 0) { registros->ebx = valor; return true; }
    if (strcmp(nombre, "ECX") == 0) { registros->ecx = valor; return true; }
    if (strcmp(nombre, "EDX") == 0) { registros->edx = valor; return true; }
    if (strcmp(nombre, "SI") == 0)  { registros->si = valor;  return true; }
    if (strcmp(nombre, "DI") == 0)  { registros->di = valor;  return true; }
    if (strcmp(nombre, "PC") == 0)  { registros->pc = valor;  return true; }

    return false;
}

>>>>>>> feature/cpu/mmu
static uint32_t parsear_uint32(const char* valor) {
    return (uint32_t) strtoul(valor, NULL, 10);
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

    registros->pc++;
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

    if (!escribir_valor_registro_cpu(registros, instruccion->parametros[0], destino + origen)) {
        return CPU_EXEC_ERROR;
    }

    registros->pc++;
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

    if (!escribir_valor_registro_cpu(registros, instruccion->parametros[0], destino - origen)) {
        return CPU_EXEC_ERROR;
    }

    registros->pc++;
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
    t_contexto* contexto,
    int socket_memory,
    uint32_t pid,
    t_log* logger
) {
    if (instruccion == NULL || registros == NULL) {
        return CPU_EXEC_ERROR;
    }

    loguear_ejecucion(instruccion, pid, logger);

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
        case CPU_INST_MOV_IN:
            return ejecutar_mov_in(instruccion, registros, contexto, segment_max_size, pid, logger);
        case CPU_INST_MOV_OUT:
            return ejecutar_mov_out(instruccion, registros, contexto, segment_max_size, pid, logger);
        default:
            return CPU_EXEC_ERROR;
    }
}

static t_resultado_ejecucion ejecutar_mov_in(t_instruccion_decodificada* instruccion, t_registros_cpu* registros, t_contexto* contexto, int socket_ms, uint32_t pid, t_log* logger) {

    if(instruccion->cantidad_parametros != 1) {
        return CPU_EXEC_ERROR;
    }

    uint32_t direccion_logica = registros->si;
    uint32_t bytes = tamanio_registro_bytes(instruccion->parametros[0]);

    t_traduccion trad;

    if(!mmu_traducir(contexto, direccion_logica, bytes, &trad)) {
        return CPU_EXEC_SEG_FAULT;
    }

    uint32_t valor = 0;

    if(!memoria_read(socket_ms, trad.direccion_fisica, bytes, &valor)) {
        return CPU_EXEC_ERROR;
    }

    escribir_registro(registros, instruccion->parametros[0], valor);

    log_info(logger, "PID: %u - Acción: LEER - Dirección Física: %u - Valor: %u", pid, trad.direccion_fisica, valor);

    registros->pc++;

    return CPU_EXEC_OK;
}

static t_resultado_ejecucion ejecutar_mov_out(t_instruccion_decodificada* instruccion, t_registros_cpu* registros, t_contexto* contexto, int socket_ms, uint32_t pid, t_log* logger) {

    if(instruccion->cantidad_parametros != 1) {
        return CPU_EXEC_ERROR;
    }

    uint32_t valor;

    if(!leer_registro(registros, instruccion->parametros[0], &valor)) {
        return CPU_EXEC_ERROR;
    }

    uint32_t direccion_logica = registros->di;
    uint32_t bytes = tamanio_registro_bytes(instruccion->parametros[0]);

    t_traduccion trad;

    if(!mmu_traducir(contexto, direccion_logica, bytes, &trad)) {
        return CPU_EXEC_SEG_FAULT;
    }

    if(!memoria_write(socket_ms, trad.direccion_fisica, bytes, &valor)) {
        return CPU_EXEC_ERROR;
    }

    log_info(logger, "PID: %u - Acción: ESCRIBIR - Dirección Física: %u - Valor: %u", pid, trad.direccion_fisica, valor);

    registros->pc++;

    return CPU_EXEC_OK;
}

static uint32_t tamanio_registro_bytes(const char* nombre) {

    if(strcmp(nombre, "AX") == 0) return 1;
    if(strcmp(nombre, "BX") == 0) return 1;
    if(strcmp(nombre, "CX") == 0) return 1;
    if(strcmp(nombre, "DX") == 0) return 1;

    return 4;
}