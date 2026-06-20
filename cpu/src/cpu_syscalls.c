#include "cpu_syscalls.h"

#include <stdlib.h>
#include <string.h>
#include <utils/protocolo.h>
#include <utils/sockets.h>

static bool esperar_ok_kernel(int socket_kernel, const char* nombre_syscall, t_log* logger) {
    t_mensaje* respuesta = recibir_mensaje(socket_kernel);
    if (respuesta == NULL) {
        log_error(logger, "Kernel Scheduler cerro la conexion durante syscall %s", nombre_syscall);
        return false;
    }

    bool ok = respuesta->op_code == MSG_OK;
    if (!ok) {
        log_error(logger, "Kernel Scheduler rechazo syscall %s", nombre_syscall);
    }

    free_mensaje(respuesta);
    return ok;
}

static bool enviar_syscall_mutex(int socket_kernel, uint32_t op_code, uint32_t pid, const char* nombre, const char* nombre_syscall, t_log* logger) {
    uint32_t nombre_size = strlen(nombre) + 1;
    uint32_t payload_size = sizeof(uint32_t) + nombre_size;
    void* payload = malloc(payload_size);

    memcpy(payload, &pid, sizeof(uint32_t));
    memcpy((char*)payload + sizeof(uint32_t), nombre, nombre_size);

    log_info(logger, "## PID: %u - Syscall: %s - %s", pid, nombre_syscall, nombre);
    enviar_mensaje(socket_kernel, op_code, payload, payload_size);

    free(payload);
    return esperar_ok_kernel(socket_kernel, nombre_syscall, logger);
}

bool enviar_syscall_mutex_create(int socket_kernel, uint32_t pid, const char* nombre, t_log* logger) {
    return enviar_syscall_mutex(socket_kernel, MSG_MUTEX_CREATE, pid, nombre, "MUTEX_CREATE", logger);
}

bool enviar_syscall_mutex_lock(int socket_kernel, uint32_t pid, const char* nombre, t_log* logger) {
    return enviar_syscall_mutex(socket_kernel, MSG_MUTEX_LOCK, pid, nombre, "MUTEX_LOCK", logger);
}

bool enviar_syscall_mutex_unlock(int socket_kernel, uint32_t pid, const char* nombre, t_log* logger) {
    return enviar_syscall_mutex(socket_kernel, MSG_MUTEX_UNLOCK, pid, nombre, "MUTEX_UNLOCK", logger);
}

bool enviar_syscall_sleep(int socket_kernel, uint32_t pid, uint32_t tiempo_ms, t_log* logger) {
    t_payload_syscall_sleep payload = {
        .pid = pid,
        .tiempo_ms = tiempo_ms
    };

    log_info(logger, "## PID: %u - Syscall: SLEEP - %u", pid, tiempo_ms);
    enviar_mensaje(socket_kernel, MSG_SYSCALL_SLEEP, &payload, sizeof(payload));

    return esperar_ok_kernel(socket_kernel, "SLEEP", logger);
}

static bool enviar_syscall_io_memoria(
    int socket_kernel,
    uint32_t op_code,
    uint32_t pid,
    uint32_t direccion_logica,
    uint32_t tamanio,
    const char* nombre_syscall,
    t_log* logger
) {
    t_payload_syscall_io_memoria payload = {
        .pid = pid,
        .direccion_logica = direccion_logica,
        .tamanio = tamanio
    };

    log_info(
        logger,
        "## PID: %u - Syscall: %s - Direccion Logica: %u - Tamanio: %u",
        pid,
        nombre_syscall,
        direccion_logica,
        tamanio
    );
    enviar_mensaje(socket_kernel, op_code, &payload, sizeof(payload));

    return esperar_ok_kernel(socket_kernel, nombre_syscall, logger);
}

bool enviar_syscall_stdout(int socket_kernel, uint32_t pid, uint32_t direccion_logica, uint32_t tamanio, t_log* logger) {
    return enviar_syscall_io_memoria(socket_kernel, MSG_SYSCALL_STDOUT, pid, direccion_logica, tamanio, "STDOUT", logger);
}

bool enviar_syscall_stdin(int socket_kernel, uint32_t pid, uint32_t direccion_logica, uint32_t tamanio, t_log* logger) {
    return enviar_syscall_io_memoria(socket_kernel, MSG_SYSCALL_STDIN, pid, direccion_logica, tamanio, "STDIN", logger);
}

bool enviar_syscall_mem_alloc(int socket_kernel, uint32_t pid, uint32_t id_segmento, uint32_t tamanio, t_log* logger) {
    t_payload_syscall_mem_alloc payload = {
        .pid = pid,
        .id_segmento = id_segmento,
        .tamanio = tamanio
    };

    log_info(
        logger,
        "## PID: %u - Syscall: MEM_ALLOC - Segmento: %u - Tamanio: %u",
        pid,
        id_segmento,
        tamanio
    );
    enviar_mensaje(socket_kernel, MSG_MEM_ALLOC, &payload, sizeof(payload));

    return true;
}

bool enviar_syscall_mem_free(int socket_kernel, uint32_t pid, uint32_t id_segmento, t_log* logger) {
    t_payload_syscall_mem_free payload = {
        .pid = pid,
        .id_segmento = id_segmento
    };

    log_info(logger, "## PID: %u - Syscall: MEM_FREE - Segmento: %u", pid, id_segmento);
    enviar_mensaje(socket_kernel, MSG_MEM_FREE, &payload, sizeof(payload));

    return true;
}

bool enviar_syscall_init_proc(int socket_kernel, uint32_t pid, const char* archivo, uint32_t prioridad, t_log* logger) {
    uint32_t archivo_size = strlen(archivo) + 1;
    uint32_t payload_size = sizeof(t_payload_syscall_init_proc) + archivo_size;
    void* payload = malloc(payload_size);
    if (payload == NULL) {
        return false;
    }

    t_payload_syscall_init_proc* pedido = (t_payload_syscall_init_proc*) payload;
    pedido->pid = pid;
    pedido->prioridad = prioridad;
    memcpy(pedido->archivo, archivo, archivo_size);

    log_info(
        logger,
        "## PID: %u - Syscall: INIT_PROC - Archivo: %s - Prioridad: %u",
        pid,
        archivo,
        prioridad
    );
    enviar_mensaje(socket_kernel, MSG_INIT_PROC, payload, payload_size);

    free(payload);
    return esperar_ok_kernel(socket_kernel, "INIT_PROC", logger);
}

bool enviar_syscall_exit(int socket_kernel, uint32_t pid, t_log* logger) {
    t_payload_syscall_exit payload = {
        .pid = pid
    };

    log_info(logger, "## PID: %u - Syscall: EXIT", pid);
    enviar_mensaje(socket_kernel, MSG_SYSCALL_EXIT, &payload, sizeof(payload));

    return esperar_ok_kernel(socket_kernel, "EXIT", logger);
}
