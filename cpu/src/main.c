#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <commons/log.h>
#include <commons/config.h>
#include <unistd.h>
#include <utils/sockets.h>
#include <utils/protocolo.h>

#include "cpu_ciclo.h"
#include "cpu_mmu.h"
#include "cpu_contexto.h"
#include "cpu_devolucion.h"
#include "cpu_dispatch.h"
#include "cpu_registros.h"
#include "cpu_handshake.h"

/*
 * Este archivo es el recorrido general de la CPU. Para explicarlo en el oral:
 * primero levanto conexiones, después espero un PID, recupero su contexto,
 * ejecuto la ráfaga y finalmente guardo y devuelvo el proceso al Scheduler.
 * La CPU no elige qué proceso corre ni qué algoritmo se usa; eso lo decide KS.
 */
int main(int argc, char* argv[]) {

    // La CPU necesita el config para las conexiones y un ID para identificarse.
    if (argc < 3) {
        printf("Uso: ./cpu [CONFIG] [ID]\n");
        return 1;
    }

    char* config_path = argv[1];
    char* cpu_id = argv[2];

    t_config* config = config_create(config_path);
    t_log* logger = log_create("cpu.log", cpu_id, 1, log_level_from_string(config_get_string_value(config, "LOG_LEVEL")));
    t_registros_cpu registros;
    inicializar_registros_cpu(&registros);

    log_info(logger, "CPU %s iniciada", cpu_id);
    log_info(logger, "Registros CPU inicializados - PC: %u", registros.pc);

    char* ip_kernel = config_get_string_value(config, "IP_KERNEL");
    int puerto_kernel = config_get_int_value(config, "PUERTO_KERNEL");

    char* ip_memory = config_get_string_value(config, "IP_MEMORY");
    int puerto_memory = config_get_int_value(config, "PUERTO_MEMORY");

    // Este valor tiene que coincidir con KM. Si no coincide, los dos módulos
    // interpretarían una misma dirección lógica como segmentos distintos.
    uint32_t tamanio_max_segmento = 256;
    if (config_has_property(config, "SEGMENT_MAX_SIZE")) {
        tamanio_max_segmento = (uint32_t) config_get_int_value(config, "SEGMENT_MAX_SIZE");
    }
    set_segment_max_size(tamanio_max_segmento);

    log_debug(logger, "Config resuelta: KS=%s:%d KM=%s:%d SEGMENT_MAX_SIZE=%u",
              ip_kernel, puerto_kernel, ip_memory, puerto_memory, tamanio_max_segmento);

     // CONEXION A KERNEL SCHEDULER
    int socket_kernel = conectar_a_servidor(ip_kernel, puerto_kernel);
    if (socket_kernel == -1) {
        log_error(logger, "No se pudo conectar a Kernel Scheduler");
    } else {
        log_info(logger, "Conectado a Kernel Scheduler");

        // Con KS mando el ID como string porque puede haber varias CPUs.
        uint32_t size;
        void* payload = serializar_string(cpu_id, &size);
        enviar_mensaje(socket_kernel, MSG_CPU_IDENTIFICACION, payload, size);
        free(payload);

        t_mensaje* respuesta_kernel = recibir_mensaje(socket_kernel);
        if (handshake_exitoso(respuesta_kernel)) {
            log_info(logger, "## Conectado a Kernel Scheduler");
        } else {
            if (respuesta_kernel == NULL)
                log_error(logger, "Kernel Scheduler cerro la conexion durante la identificacion");
            else
                log_error(logger, "Kernel Scheduler rechazo la identificacion");
            socket_kernel = -1;
        }

        free_mensaje(respuesta_kernel);
    }

    // CONEXION A KERNEL MEMORY
    int socket_memory = conectar_a_servidor(ip_memory, puerto_memory);
    if (socket_memory == -1) {
        log_error(logger, "No se pudo conectar a Kernel Memory");
    } else {
        log_info(logger, "Conectado a Kernel Memory");

        // A KM sólo le aviso que soy una CPU; por eso acá no hace falta payload.
        enviar_mensaje(socket_memory, MSG_CPU_IDENTIFICACION, NULL, 0);

        t_mensaje* respuesta = recibir_mensaje(socket_memory);
        if (handshake_exitoso(respuesta)) {
            log_info(logger, "## Conectado a Kernel Memory");
        } else {
            if (respuesta == NULL)
                log_error(logger, "Kernel Memory cerro la conexion durante el handshake");
            else
                log_error(logger, "Kernel Memory rechazo la conexion");
            socket_memory = -1;
        }

        free_mensaje(respuesta);
    }

    // Conectar a todos los Memory Sticks configurados.
    // El índice en el array corresponde al id_memory_stick que asigna KM.
    // Config: IP_MEMORY_STICK_0 / PUERTO_MEMORY_STICK_0, luego _1, _2, ...
    // Si no existe _0, se intenta con las claves sin índice (compatibilidad
    // con configs de un solo MS: IP_MEMORY_STICK / PUERTO_MEMORY_STICK).
#define CPU_MAX_MS 8
    // La posición del fd es importante: sockets_ms[0] corresponde al MS 0,
    // sockets_ms[1] al MS 1, etc. La tabla de segmentos me dice cuál usar.
    int sockets_ms[CPU_MAX_MS];
    int n_sockets_ms = 0;
    for (int i = 0; i < CPU_MAX_MS; i++) sockets_ms[i] = -1;

    for (int idx = 0; idx < CPU_MAX_MS; idx++) {
        char key_ip[32], key_puerto[32];
        snprintf(key_ip,    sizeof(key_ip),    "IP_MEMORY_STICK_%d", idx);
        snprintf(key_puerto, sizeof(key_puerto), "PUERTO_MEMORY_STICK_%d", idx);

        if (!config_has_property(config, key_ip)) {
            if (idx == 0 && config_has_property(config, "IP_MEMORY_STICK")) {
                snprintf(key_ip,    sizeof(key_ip),    "%s", "IP_MEMORY_STICK");
                snprintf(key_puerto, sizeof(key_puerto), "%s", "PUERTO_MEMORY_STICK");
            } else {
                break;
            }
        }

        char* ip_ms    = config_get_string_value(config, key_ip);
        int   puerto_ms = config_get_int_value(config, key_puerto);

        int fd = conectar_a_servidor(ip_ms, puerto_ms);
        if (fd == -1) {
            log_error(logger, "No se pudo conectar a Memory Stick %d (%s:%d)", idx, ip_ms, puerto_ms);
            continue;
        }

        // Los enteros que viajan por socket se mandan en byte order de red.
        uint32_t cpu_id_n = htonl((uint32_t) atoi(cpu_id));
        enviar_mensaje(fd, MSG_CPU_IDENTIFICACION, &cpu_id_n, sizeof(cpu_id_n));

        t_mensaje* resp_ms = recibir_mensaje(fd);
        if (!handshake_exitoso(resp_ms)) {
            log_error(logger, "Memory Stick %d rechazó la conexión", idx);
            if (resp_ms) free_mensaje(resp_ms);
            close(fd);
            continue;
        }
        free_mensaje(resp_ms);

        log_info(logger, "## Conectado a Memory Stick %d", idx);
        sockets_ms[idx] = fd;
        n_sockets_ms = idx + 1;
    }

    // Este while representa las distintas ráfagas que atiende una misma CPU.
    while (socket_kernel != -1 && socket_memory != -1) {
        uint32_t pid;

        // Me quedo bloqueado hasta que KS elija un proceso y mande su PID.
        if (!recibir_proceso_a_ejecutar(socket_kernel, &pid, logger)) {
            break;
        }

        // El contexto vive en KM. Lo traigo al empezar para continuar exactamente
        // desde el PC y los registros que dejó la ráfaga anterior.
        t_contexto* contexto = NULL;
        if (!restaurar_contexto_desde_memory(socket_memory, pid, &contexto, &registros, logger)) {
            log_error(logger, "No se pudo restaurar contexto para PID %u", pid);
            break;
        }

        // Acá ocurre fetch-decode-execute hasta que aparece un motivo de corte.
        t_resultado_ciclo_cpu resultado_ciclo = ejecutar_ciclo_proceso(
            socket_kernel,
            socket_memory,
            sockets_ms,
            n_sockets_ms,
            pid,
            contexto,
            &registros,
            logger
        );
        // El ciclo usa resultados internos; antes de hablar con KS los traduzco
        // a los motivos que forman parte del protocolo compartido.
        t_motivo_devolucion_cpu motivo_devolucion;
        if (resultado_ciclo == CPU_CICLO_SYSCALL) {
            motivo_devolucion = MOTIVO_DEVOLUCION_SYSCALL;
        } else if (resultado_ciclo == CPU_CICLO_EXIT) {
            motivo_devolucion = MOTIVO_DEVOLUCION_EXIT;
        } else if (resultado_ciclo == CPU_CICLO_INTERRUPCION) {
            motivo_devolucion = MOTIVO_DEVOLUCION_INTERRUPCION;
        } else if (resultado_ciclo == CPU_CICLO_SEG_FAULT) {
            motivo_devolucion = MOTIVO_DEVOLUCION_SEG_FAULT;
        } else {
            motivo_devolucion = MOTIVO_DEVOLUCION_ERROR;
        }

        // Primero guardo el contexto y recién después devuelvo el proceso. Así KS
        // puede volver a planificarlo sin perder el avance de registros y PC.
        if (!guardar_contexto_en_memory(socket_memory, contexto, &registros, logger)) {
            motivo_devolucion = MOTIVO_DEVOLUCION_ERROR;
        }
        liberar_contexto_cpu(contexto);

        devolver_proceso_a_scheduler(socket_kernel, pid, motivo_devolucion, &registros, logger);

        if (motivo_devolucion == MOTIVO_DEVOLUCION_ERROR) {
            log_error(logger, "Fallo el ciclo de instruccion para PID %u", pid);
            break;
        }
    }

    close(socket_kernel);
    close(socket_memory);
    for (int i = 0; i < CPU_MAX_MS; i++) {
        if (sockets_ms[i] != -1) close(sockets_ms[i]);
    }

    log_destroy(logger);
    config_destroy(config);

    return 0;
}
