#include "cpu_memoria.h"

#include <arpa/inet.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <utils/protocolo.h>
#include <utils/sockets.h>

#include "cpu_logs.h"
#include "cpu_mmu.h"

/*
 * Acá se ejecutan MOV_IN, MOV_OUT y COPY_MEM. El orden siempre es el mismo:
 * obtener dirección/tamaño, pedir traducción a MMU, elegir el MS y recién ahí
 * leer o escribir. Si MMU falla, el pedido nunca llega a memoria física.
 */

// Los registros de 32 bits también se convierten a byte order de red. Para un
// registro de 8 bits no hace falta porque es un solo byte.
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

bool memoria_read(int socket_memory, uint32_t direccion, uint32_t tamanio, void* valor) {
    // Aunque el parámetro se llama socket_memory, desde CPU este fd es el del
    // Memory Stick elegido por la tabla de segmentos.
    if (socket_memory < 0 || valor == NULL || tamanio == 0) {
        return false;
    }

    t_payload_leer_memoria pedido = {
        .dir_fisica = htonl(direccion),
        .tamanio = htonl(tamanio)
    };

    // El MS recibe dirección física y cantidad; los bytes vienen en la respuesta.
    enviar_mensaje(socket_memory, MSG_MEMORY_READ, &pedido, sizeof(pedido));

    t_mensaje* respuesta = recibir_mensaje(socket_memory);
    if (respuesta == NULL) {
        return false;
    }

    bool ok = respuesta->op_code == MSG_MEMORY_READ_RESPUESTA &&
              respuesta->payload != NULL &&
              respuesta->payload_size == tamanio;
    if (ok) {
        memcpy(valor, respuesta->payload, tamanio);
    }

    free_mensaje(respuesta);
    return ok;
}

bool memoria_write(int socket_memory, uint32_t direccion, uint32_t tamanio, const void* datos) {
    if (socket_memory < 0 || datos == NULL || tamanio == 0 ||
        tamanio > UINT32_MAX - sizeof(t_payload_escribir_memoria)) {
        return false;
    }

    // En write el payload lleva el encabezado y, pegados atrás, los datos.
    uint32_t payload_size = sizeof(t_payload_escribir_memoria) + tamanio;
    void* payload = malloc(payload_size);
    if (payload == NULL) {
        return false;
    }

    uint32_t direccion_n = htonl(direccion);
    uint32_t tamanio_n = htonl(tamanio);
    memcpy(payload, &direccion_n, sizeof(uint32_t));
    memcpy((uint8_t*) payload + sizeof(uint32_t), &tamanio_n, sizeof(uint32_t));
    memcpy((uint8_t*) payload + sizeof(t_payload_escribir_memoria), datos, tamanio);

    enviar_mensaje(socket_memory, MSG_MEMORY_WRITE, payload, payload_size);
    free(payload);

    t_mensaje* respuesta = recibir_mensaje(socket_memory);
    if (respuesta == NULL) {
        return false;
    }

    // La escritura no devuelve contenido: alcanza con la confirmación de MS.
    bool ok = respuesta->op_code == MSG_OK;
    free_mensaje(respuesta);
    return ok;
}

static t_resultado_memoria_cpu traducir_o_fallar(
    t_contexto* contexto,
    uint32_t direccion_logica,
    uint32_t tamanio,
    t_traduccion_mmu* traduccion,
    t_log* logger
) {
    t_resultado_mmu resultado = traducir_direccion_logica(
        contexto,
        direccion_logica,
        tamanio,
        traduccion
    );

    if (resultado == CPU_MMU_OK) {
        log_debug(logger,
            "MMU: dir_logica=%u -> segmento=%u desplazamiento=%u memory_stick=%u dir_fisica=%u limite_segmento=%u",
            direccion_logica, traduccion->id_segmento, traduccion->desplazamiento,
            traduccion->id_memory_stick, traduccion->direccion_fisica, traduccion->limite_segmento);
        return CPU_MEMORIA_OK;
    }

    log_error(logger, "Fallo MMU: %s", resultado_mmu_to_string(resultado));
    // Para el proceso, tanto pasarse del límite como pedir un segmento que no
    // existe son accesos inválidos y se informan como segmentation fault.
    if (resultado == CPU_MMU_SEGMENTATION_FAULT || resultado == CPU_MMU_SEGMENTO_NO_ENCONTRADO) {
        return CPU_MEMORIA_SEG_FAULT;
    }

    return CPU_MEMORIA_ERROR;
}

static bool resolver_inicio(
    t_cpu_memory_sticks* memory_sticks,
    uint32_t id_memory_stick,
    uint32_t direccion,
    t_cpu_memory_stick_resuelto* stick
) {
    if (!cpu_memory_sticks_resolver_id(memory_sticks, id_memory_stick, stick)) {
        return false;
    }

    if (!stick->rango_conocido ||
        (direccion >= stick->base_global &&
         (uint64_t) direccion < (uint64_t) stick->base_global + stick->tamanio)) {
        return true;
    }

    return cpu_memory_sticks_resolver_direccion(memory_sticks, direccion, stick);
}

static uint32_t tamanio_fragmento(
    const t_cpu_memory_stick_resuelto* stick,
    uint32_t direccion,
    uint32_t pendiente
) {
    if (!stick->rango_conocido) {
        return pendiente;
    }
    uint64_t fin = (uint64_t) stick->base_global + stick->tamanio;
    if ((uint64_t) direccion >= fin) {
        return 0;
    }
    uint64_t disponible = fin - direccion;
    return disponible < pendiente ? (uint32_t) disponible : pendiente;
}

static bool memoria_read_fragmentada(
    t_cpu_memory_sticks* memory_sticks,
    uint32_t id_memory_stick,
    uint32_t direccion,
    uint32_t tamanio,
    void* valor
) {
    if (memory_sticks == NULL || valor == NULL || tamanio == 0 ||
        direccion > UINT32_MAX - (tamanio - 1)) {
        return false;
    }

    t_cpu_memory_stick_resuelto stick;
    if (!resolver_inicio(memory_sticks, id_memory_stick, direccion, &stick)) {
        return false;
    }

    uint32_t procesado = 0;
    while (procesado < tamanio) {
        uint32_t actual = direccion + procesado;
        if (stick.rango_conocido &&
            (actual < stick.base_global ||
             (uint64_t) actual >= (uint64_t) stick.base_global + stick.tamanio)) {
            if (!cpu_memory_sticks_resolver_direccion(memory_sticks, actual, &stick)) {
                return false;
            }
        }

        uint32_t fragmento = tamanio_fragmento(&stick, actual, tamanio - procesado);
        if (fragmento == 0 ||
            !memoria_read(stick.socket, actual, fragmento, (uint8_t*) valor + procesado)) {
            cpu_memory_sticks_invalidar_socket(
                memory_sticks,
                stick.id_memory_stick,
                stick.socket
            );
            return false;
        }
        procesado += fragmento;
    }
    return true;
}

static bool memoria_write_fragmentada(
    t_cpu_memory_sticks* memory_sticks,
    uint32_t id_memory_stick,
    uint32_t direccion,
    uint32_t tamanio,
    const void* datos
) {
    if (memory_sticks == NULL || datos == NULL || tamanio == 0 ||
        direccion > UINT32_MAX - (tamanio - 1)) {
        return false;
    }

    t_cpu_memory_stick_resuelto stick;
    if (!resolver_inicio(memory_sticks, id_memory_stick, direccion, &stick)) {
        return false;
    }

    uint32_t procesado = 0;
    while (procesado < tamanio) {
        uint32_t actual = direccion + procesado;
        if (stick.rango_conocido &&
            (actual < stick.base_global ||
             (uint64_t) actual >= (uint64_t) stick.base_global + stick.tamanio)) {
            if (!cpu_memory_sticks_resolver_direccion(memory_sticks, actual, &stick)) {
                return false;
            }
        }

        uint32_t fragmento = tamanio_fragmento(&stick, actual, tamanio - procesado);
        if (fragmento == 0 ||
            !memoria_write(
                stick.socket,
                actual,
                fragmento,
                (const uint8_t*) datos + procesado
            )) {
            cpu_memory_sticks_invalidar_socket(
                memory_sticks,
                stick.id_memory_stick,
                stick.socket
            );
            return false;
        }
        procesado += fragmento;
    }
    return true;
}

static t_resultado_memoria_cpu ejecutar_mov_in(
    t_cpu_memory_sticks* memory_sticks,
    t_instruccion_decodificada* instruccion,
    t_contexto* contexto,
    t_registros_cpu* registros,
    uint32_t pid,
    t_log* logger
) {
    if (instruccion->cantidad_parametros != 1) {
        return CPU_MEMORIA_ERROR;
    }

    // MOV_IN trae memoria -> registro. La dirección lógica está siempre en SI
    // y la cantidad de bytes depende del registro destino (1 o 4).
    uint32_t tamanio_registro;
    if (!tamanio_registro_cpu(instruccion->parametros[0], &tamanio_registro)) {
        return CPU_MEMORIA_ERROR;
    }

    t_traduccion_mmu traduccion;
    t_resultado_memoria_cpu resultado = traducir_o_fallar(
        contexto,
        registros->si,
        tamanio_registro,
        &traduccion,
        logger
    );
    if (resultado != CPU_MEMORIA_OK) {
        return resultado;
    }

    uint8_t buffer[sizeof(uint32_t)] = {0};
    if (!memoria_read_fragmentada(
        memory_sticks,
        traduccion.id_memory_stick,
        traduccion.direccion_fisica,
        tamanio_registro,
        buffer
    )) {
        return CPU_MEMORIA_ERROR;
    }

    // Recién después de una lectura válida convierto y modifico el registro.
    uint32_t valor = bytes_a_valor(buffer, tamanio_registro);
    if (!escribir_valor_registro_cpu(registros, instruccion->parametros[0], valor)) {
        return CPU_MEMORIA_ERROR;
    }

    log_cpu_acceso_memoria(logger, pid, "LEER", traduccion.direccion_fisica, valor);
    // Si MOV_IN escribió PC, ese valor ya indica la próxima instrucción.
    if (strcmp(instruccion->parametros[0], "PC") != 0) {
        registros->pc++;
    }
    return CPU_MEMORIA_OK;
}

static t_resultado_memoria_cpu ejecutar_mov_out(
    t_cpu_memory_sticks* memory_sticks,
    t_instruccion_decodificada* instruccion,
    t_contexto* contexto,
    t_registros_cpu* registros,
    uint32_t pid,
    t_log* logger
) {
    if (instruccion->cantidad_parametros != 1) {
        return CPU_MEMORIA_ERROR;
    }

    // MOV_OUT hace el camino inverso: registro -> memoria. La dirección lógica
    // de destino sale de DI.
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
        &traduccion,
        logger
    );
    if (resultado != CPU_MEMORIA_OK) {
        return resultado;
    }

    uint8_t buffer[sizeof(uint32_t)] = {0};
    // Convierto el valor al formato que espera el otro extremo antes de enviarlo.
    valor_a_bytes(valor, tamanio_registro, buffer);
    if (!memoria_write_fragmentada(
        memory_sticks,
        traduccion.id_memory_stick,
        traduccion.direccion_fisica,
        tamanio_registro,
        buffer
    )) {
        return CPU_MEMORIA_ERROR;
    }

    log_cpu_acceso_memoria(logger, pid, "ESCRIBIR", traduccion.direccion_fisica, valor);
    registros->pc++;
    return CPU_MEMORIA_OK;
}

static t_resultado_memoria_cpu ejecutar_copy_mem(
    t_cpu_memory_sticks* memory_sticks,
    t_instruccion_decodificada* instruccion,
    t_contexto* contexto,
    t_registros_cpu* registros,
    uint32_t pid,
    t_log* logger
) {
    if (instruccion->cantidad_parametros != 1) {
        return CPU_MEMORIA_ERROR;
    }

    // El único parámetro de COPY_MEM es un registro cuyo valor indica cuántos
    // bytes copiar. SI es origen y DI es destino.
    uint32_t tamanio;
    if (!leer_valor_registro_cpu(registros, instruccion->parametros[0], &tamanio) || tamanio == 0) {
        return CPU_MEMORIA_ERROR;
    }

    // Traduzco por separado porque cada dirección puede pertenecer a un segmento
    // e incluso a un Memory Stick distinto.
    t_traduccion_mmu origen;
    t_resultado_memoria_cpu resultado_origen = traducir_o_fallar(
        contexto,
        registros->si,
        tamanio,
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
        &destino,
        logger
    );
    if (resultado_destino != CPU_MEMORIA_OK) {
        return resultado_destino;
    }

    // Uso un buffer temporal: primero completo la lectura y después escribo.
    uint8_t* buffer = malloc(tamanio);
    if (buffer == NULL) {
        return CPU_MEMORIA_ERROR;
    }

    if (!memoria_read_fragmentada(
        memory_sticks,
        origen.id_memory_stick,
        origen.direccion_fisica,
        tamanio,
        buffer
    )) {
        free(buffer);
        return CPU_MEMORIA_ERROR;
    }

    if (!memoria_write_fragmentada(
        memory_sticks,
        destino.id_memory_stick,
        destino.direccion_fisica,
        tamanio,
        buffer
    )) {
        free(buffer);
        return CPU_MEMORIA_ERROR;
    }

    // En COPY_MEM el último campo del log guarda el tamaño, no el contenido,
    // porque se está moviendo un bloque y no un único valor de registro.
    log_cpu_acceso_memoria(logger, pid, "LEER", origen.direccion_fisica, tamanio);
    log_cpu_acceso_memoria(logger, pid, "ESCRIBIR", destino.direccion_fisica, tamanio);
    free(buffer);
    registros->pc++;
    return CPU_MEMORIA_OK;
}

t_resultado_memoria_cpu ejecutar_instruccion_memoria(
    t_cpu_memory_sticks* memory_sticks,
    t_instruccion_decodificada* instruccion,
    t_contexto* contexto,
    t_registros_cpu* registros,
    uint32_t pid,
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

    // Este switch es el execute específico de las instrucciones de memoria.
    switch (instruccion->opcode) {
        case CPU_INST_MOV_IN:
            return ejecutar_mov_in(memory_sticks, instruccion, contexto, registros, pid, logger);
        case CPU_INST_MOV_OUT:
            return ejecutar_mov_out(memory_sticks, instruccion, contexto, registros, pid, logger);
        case CPU_INST_COPY_MEM:
            return ejecutar_copy_mem(memory_sticks, instruccion, contexto, registros, pid, logger);
        default:
            return CPU_MEMORIA_ERROR;
    }
}
