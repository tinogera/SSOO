#include "cpu_memoria.h"

#include <arpa/inet.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <utils/protocolo.h>
#include <utils/sockets.h>

#include "cpu_logs.h"
#include "cpu_mmu.h"

static void valor_a_bytes(uint32_t valor, uint32_t tamanio, uint8_t* destino) {
    if (tamanio == 1) {
        destino[0] = (uint8_t) valor;
        return;
    }

    uint32_t valor_n = htonl(valor);
    memcpy(destino, &valor_n, sizeof(uint32_t));
}

static uint32_t bytes_a_valor(uint8_t* origen, uint32_t tamanio) {
    if (tamanio == 1) {
        return origen[0];
    }

    uint32_t valor_n;
    memcpy(&valor_n, origen, sizeof(uint32_t));
    return ntohl(valor_n);
}

static bool leer_bytes_memoria(int socket_memoria, uint32_t direccion_fisica, uint32_t tamanio, uint8_t* destino) {
    if (socket_memoria < 0 || destino == NULL) {
        return false;
    }

    t_payload_leer_memoria pedido = {
        .direccion_fisica = htonl(direccion_fisica),
        .tamanio = htonl(tamanio)
    };

    enviar_mensaje(socket_memoria, MSG_LEER_MEMORIA, &pedido, sizeof(pedido));

    t_mensaje* respuesta = recibir_mensaje(socket_memoria);
    if (respuesta == NULL) {
        return false;
    }

    bool ok = respuesta->op_code == MSG_RESPUESTA_LEER_MEMORIA &&
              respuesta->payload_size >= tamanio;
    if (ok) {
        memcpy(destino, respuesta->payload, tamanio);
    }

    free_mensaje(respuesta);
    return ok;
}

static bool escribir_bytes_memoria(int socket_memoria, uint32_t direccion_fisica, uint32_t tamanio, uint8_t* datos) {
    if (socket_memoria < 0 || datos == NULL) {
        return false;
    }

    uint32_t payload_size = sizeof(t_payload_escribir_memoria) + tamanio;
    void* payload = malloc(payload_size);
    if (payload == NULL) {
        return false;
    }

    uint32_t direccion_n = htonl(direccion_fisica);
    uint32_t tamanio_n = htonl(tamanio);
    memcpy(payload, &direccion_n, sizeof(uint32_t));
    memcpy((uint8_t*) payload + sizeof(uint32_t), &tamanio_n, sizeof(uint32_t));
    memcpy((uint8_t*) payload + sizeof(t_payload_escribir_memoria), datos, tamanio);

    enviar_mensaje(socket_memoria, MSG_ESCRIBIR_MEMORIA, payload, payload_size);
    free(payload);

    t_mensaje* respuesta = recibir_mensaje(socket_memoria);
    if (respuesta == NULL) {
        return false;
    }

    bool ok = respuesta->op_code == MSG_OK;
    free_mensaje(respuesta);
    return ok;
}

static t_resultado_memoria_cpu traducir_o_fallar(
    t_contexto* contexto,
    uint32_t direccion_logica,
    uint32_t tamanio,
    uint32_t tamanio_max_segmento,
    t_traduccion_mmu* traduccion,
    t_log* logger
) {
    t_resultado_mmu resultado = traducir_direccion_logica(
        contexto,
        direccion_logica,
        tamanio,
        tamanio_max_segmento,
        traduccion
    );

    if (resultado == CPU_MMU_OK) {
        return CPU_MEMORIA_OK;
    }

    log_error(logger, "Fallo MMU: %s", resultado_mmu_to_string(resultado));
    if (resultado == CPU_MMU_SEGMENTATION_FAULT || resultado == CPU_MMU_SEGMENTO_NO_ENCONTRADO) {
        return CPU_MEMORIA_SEG_FAULT;
    }

    return CPU_MEMORIA_ERROR;
}

static t_resultado_memoria_cpu ejecutar_mov_in(
    int socket_memoria,
    t_instruccion_decodificada* instruccion,
    t_contexto* contexto,
    t_registros_cpu* registros,
    uint32_t pid,
    uint32_t tamanio_max_segmento,
    t_log* logger
) {
    if (instruccion->cantidad_parametros != 1) {
        return CPU_MEMORIA_ERROR;
    }

    uint32_t tamanio_registro;
    if (!tamanio_registro_cpu(instruccion->parametros[0], &tamanio_registro)) {
        return CPU_MEMORIA_ERROR;
    }

    t_traduccion_mmu traduccion;
    t_resultado_memoria_cpu resultado = traducir_o_fallar(
        contexto,
        registros->si,
        tamanio_registro,
        tamanio_max_segmento,
        &traduccion,
        logger
    );
    if (resultado != CPU_MEMORIA_OK) {
        return resultado;
    }

    uint8_t buffer[sizeof(uint32_t)] = {0};
    if (!leer_bytes_memoria(socket_memoria, traduccion.direccion_fisica, tamanio_registro, buffer)) {
        return CPU_MEMORIA_ERROR;
    }

    uint32_t valor = bytes_a_valor(buffer, tamanio_registro);
    if (!escribir_valor_registro_cpu(registros, instruccion->parametros[0], valor)) {
        return CPU_MEMORIA_ERROR;
    }

    log_cpu_acceso_memoria(logger, pid, "LEER", traduccion.direccion_fisica, valor);
    registros->pc++;
    return CPU_MEMORIA_OK;
}

static t_resultado_memoria_cpu ejecutar_mov_out(
    int socket_memoria,
    t_instruccion_decodificada* instruccion,
    t_contexto* contexto,
    t_registros_cpu* registros,
    uint32_t pid,
    uint32_t tamanio_max_segmento,
    t_log* logger
) {
    if (instruccion->cantidad_parametros != 1) {
        return CPU_MEMORIA_ERROR;
    }

    uint32_t tamanio_registro;
    uint32_t valor;
    if (
        !tamanio_registro_cpu(instruccion->parametros[0], &tamanio_registro) ||
        !leer_valor_registro_cpu(registros, instruccion->parametros[0], &valor)
    ) {
        return CPU_MEMORIA_ERROR;
    }

    t_traduccion_mmu traduccion;
    t_resultado_memoria_cpu resultado = traducir_o_fallar(
        contexto,
        registros->di,
        tamanio_registro,
        tamanio_max_segmento,
        &traduccion,
        logger
    );
    if (resultado != CPU_MEMORIA_OK) {
        return resultado;
    }

    uint8_t buffer[sizeof(uint32_t)] = {0};
    valor_a_bytes(valor, tamanio_registro, buffer);
    if (!escribir_bytes_memoria(socket_memoria, traduccion.direccion_fisica, tamanio_registro, buffer)) {
        return CPU_MEMORIA_ERROR;
    }

    log_cpu_acceso_memoria(logger, pid, "ESCRIBIR", traduccion.direccion_fisica, valor);
    registros->pc++;
    return CPU_MEMORIA_OK;
}

static t_resultado_memoria_cpu ejecutar_copy_mem(
    int socket_memoria,
    t_instruccion_decodificada* instruccion,
    t_contexto* contexto,
    t_registros_cpu* registros,
    uint32_t pid,
    uint32_t tamanio_max_segmento,
    t_log* logger
) {
    if (instruccion->cantidad_parametros != 1) {
        return CPU_MEMORIA_ERROR;
    }

    uint32_t tamanio;
    if (!leer_valor_registro_cpu(registros, instruccion->parametros[0], &tamanio) || tamanio == 0) {
        return CPU_MEMORIA_ERROR;
    }

    t_traduccion_mmu origen;
    t_resultado_memoria_cpu resultado_origen = traducir_o_fallar(
        contexto,
        registros->si,
        tamanio,
        tamanio_max_segmento,
        &origen,
        logger
    );
    if (resultado_origen != CPU_MEMORIA_OK) {
        return resultado_origen;
    }

    t_traduccion_mmu destino;
    t_resultado_memoria_cpu resultado_destino = traducir_o_fallar(
        contexto,
        registros->di,
        tamanio,
        tamanio_max_segmento,
        &destino,
        logger
    );
    if (resultado_destino != CPU_MEMORIA_OK) {
        return resultado_destino;
    }

    uint8_t* buffer = malloc(tamanio);
    if (buffer == NULL) {
        return CPU_MEMORIA_ERROR;
    }

    if (!leer_bytes_memoria(socket_memoria, origen.direccion_fisica, tamanio, buffer)) {
        free(buffer);
        return CPU_MEMORIA_ERROR;
    }

    if (!escribir_bytes_memoria(socket_memoria, destino.direccion_fisica, tamanio, buffer)) {
        free(buffer);
        return CPU_MEMORIA_ERROR;
    }

    log_cpu_acceso_memoria(logger, pid, "LEER", origen.direccion_fisica, tamanio);
    log_cpu_acceso_memoria(logger, pid, "ESCRIBIR", destino.direccion_fisica, tamanio);
    free(buffer);
    registros->pc++;
    return CPU_MEMORIA_OK;
}

t_resultado_memoria_cpu ejecutar_instruccion_memoria(
    int socket_memoria,
    t_instruccion_decodificada* instruccion,
    t_contexto* contexto,
    t_registros_cpu* registros,
    uint32_t pid,
    uint32_t tamanio_max_segmento,
    t_log* logger
) {
    if (instruccion == NULL || contexto == NULL || registros == NULL) {
        return CPU_MEMORIA_ERROR;
    }

    char parametros[CPU_MAX_PARAMETROS * CPU_MAX_PARAMETRO_LENGTH] = "";
    for (int i = 0; i < instruccion->cantidad_parametros; i++) {
        if (i > 0) {
            strncat(parametros, " ", sizeof(parametros) - strlen(parametros) - 1);
        }
        strncat(parametros, instruccion->parametros[i], sizeof(parametros) - strlen(parametros) - 1);
    }
    log_cpu_ejecucion(logger, pid, opcode_cpu_to_string(instruccion->opcode), parametros);

    switch (instruccion->opcode) {
        case CPU_INST_MOV_IN:
            return ejecutar_mov_in(socket_memoria, instruccion, contexto, registros, pid, tamanio_max_segmento, logger);
        case CPU_INST_MOV_OUT:
            return ejecutar_mov_out(socket_memoria, instruccion, contexto, registros, pid, tamanio_max_segmento, logger);
        case CPU_INST_COPY_MEM:
            return ejecutar_copy_mem(socket_memoria, instruccion, contexto, registros, pid, tamanio_max_segmento, logger);
        default:
            return CPU_MEMORIA_ERROR;
    }
}
