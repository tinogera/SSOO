#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <commons/log.h>
#include <commons/config.h>
#include <utils/sockets.h>
#include <utils/protocolo.h>
#include <pthread.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>

void* atender_cpu(void* arg);
void* atender_kernel_memory(void* arg);
// direccion_global: true si la dirección que llega es global (viene directo
// de la CPU, vía MOV_IN/MOV_OUT/COPY_MEM) y hay que restarle offset_global_ms
// antes de indexar el buffer propio; false si ya viene local (KM, que ya hace
// la traducción global->local del lado de kernel_memory antes de pedir).
void manejar_write(int fd_cpu, t_mensaje* msg, bool direccion_global);
void manejar_read(int fd_cpu, t_mensaje* msg, bool direccion_global);
void manejar_read_cpu(int fd_cpu, t_mensaje* msg);

static uint32_t delay_ms;

// Dónde empieza este stick dentro del espacio global de direcciones físicas.
// Lo informa Kernel Memory en la respuesta del handshake de identificación.
uint32_t offset_global_ms = 0;

typedef struct {
    int fd_cpu;
} t_cpu_args;

typedef struct {

    void* buffer;

    uint32_t tamanio;

    pthread_mutex_t mutex;

} t_memory_stick;

t_log* logger;
t_memory_stick memoria_global;

static void aplicar_memory_delay(void) {
    struct timespec restante = {
        .tv_sec = delay_ms / 1000u,
        .tv_nsec = (long)(delay_ms % 1000u) * 1000000L
    };

    while (nanosleep(&restante, &restante) == -1 && errno == EINTR) {
    }
}

static bool resolver_rango(uint32_t direccion_recibida, uint32_t tamanio,
                           bool direccion_global, uint32_t* direccion_local) {
    if (direccion_local == NULL || tamanio == 0) return false;

    uint32_t local = direccion_recibida;
    if (direccion_global) {
        if (direccion_recibida < offset_global_ms) return false;
        local = direccion_recibida - offset_global_ms;
    }

    if (local > memoria_global.tamanio) return false;
    if (tamanio > memoria_global.tamanio - local) return false;

    *direccion_local = local;
    return true;
}

int main(int argc, char* argv[]) {
    // -------------------------------------------------------------------
    // 1. Validar argumentos
    // -------------------------------------------------------------------


    if (argc < 3) {
        fprintf(stderr, "Uso: %s [Archivo Config] [Tamaño]\n", argv[0]);
        return EXIT_FAILURE;
    }

    char* config_path = argv[1];
    char* fin_tamanio = NULL;
    errno = 0;
    unsigned long tamanio_parseado = strtoul(argv[2], &fin_tamanio, 10);
    if (errno != 0 || fin_tamanio == argv[2] || *fin_tamanio != '\0' ||
        tamanio_parseado == 0 || tamanio_parseado > UINT32_MAX) {
        fprintf(stderr, "Tamaño inválido\n");
        return EXIT_FAILURE;
    }
    uint32_t tamanio = (uint32_t)tamanio_parseado;

    // -------------------------------------------------------------------
    // 2. Leer configuración y cargar variables
    // -------------------------------------------------------------------

    t_config* config = config_create(config_path);
    if (config == NULL) {
        fprintf(stderr, "No se pudo leer el archivo de configuración: %s\n", config_path);
        return EXIT_FAILURE;
    }

    memoria_global.buffer = malloc(tamanio);
    if (memoria_global.buffer == NULL) {
        fprintf(stderr, "No se pudo reservar memoria\n");
        config_destroy(config);
        return EXIT_FAILURE;
    }
    memoria_global.tamanio = tamanio;
    if (pthread_mutex_init(&memoria_global.mutex, NULL) != 0) {
        fprintf(stderr, "No se pudo inicializar el mutex de memoria\n");
        free(memoria_global.buffer);
        config_destroy(config);
        return EXIT_FAILURE;
    }


    // char * ip         = config_get_string_value(config,    "MEMORY_STICK_IP");
    int   puerto      = config_get_int_value(config,    "MEMORY_STICK_PORT");
    int   delay_config = config_get_int_value(config,   "MEMORY_DELAY");
    int   kernel_port = config_get_int_value(config,    "KERNEL_MEMORY_PORT");
    char* kernel_ip   = config_get_string_value(config, "KERNEL_MEMORY_IP");
    char* logLevel    = config_get_string_value(config, "LOG_LEVEL");
    if (puerto <= 0 || puerto > UINT16_MAX || kernel_port <= 0 || kernel_port > UINT16_MAX ||
        delay_config < 0 || kernel_ip == NULL || logLevel == NULL) {
        fprintf(stderr, "Configuración inválida o incompleta\n");
        pthread_mutex_destroy(&memoria_global.mutex);
        free(memoria_global.buffer);
        config_destroy(config);
        return EXIT_FAILURE;
    }
    delay_ms = (uint32_t)delay_config;
    // -------------------------------------------------------------------
    // 3. Inicializar logger
    // -------------------------------------------------------------------

    // Se usa el puerto propio como identificador del archivo de log: es el
    // único dato disponible en este punto (antes de conectar a KM) que ya
    // está garantizado como único por instancia — dos sticks en la misma
    // máquina no pueden compartir puerto. Un contador local (id) no serviría:
    // cada instancia de memory_stick es un proceso aparte, así que un
    // contador que arranca en 0 en cada uno siempre da el mismo valor.
    char log_file[64];
    snprintf(log_file, sizeof(log_file), "memory_stick_%d.log", puerto);

    logger = log_create(log_file, "MemoryStick", true, log_level_from_string(logLevel));
    if (logger == NULL) {
        fprintf(stderr, "Error al crear el logger\n");
        pthread_mutex_destroy(&memoria_global.mutex);
        free(memoria_global.buffer);
        config_destroy(config);
        return EXIT_FAILURE;
    }

    log_info(logger, "Se creo log en memory_stick_%d.log", puerto);
    // -------------------------------------------------------------------
    // 4. Conectarse al Kernel Memory
    // -------------------------------------------------------------------

    if (kernel_ip == NULL) {
        log_error(logger, "Falta KERNEL_MEMORY_IP en el archivo de configuración");
        pthread_mutex_destroy(&memoria_global.mutex);
        free(memoria_global.buffer);
        config_destroy(config);
        log_destroy(logger);
        return EXIT_FAILURE;
    }

    // int fd = conectar_a_servidor(kernel_ip, kernel_port);
    // log_info(logger, "Kernel IP: %s", kernel_ip);
    // if (fd < 0) {
    //     log_error(logger, "Kernel Memory no esta levantado o los datos son incorrectos\n");
    // }
    // log_info(logger, "## Conectado a Kernel Memory\n");

    // -------------------------------------------------------------------
    // 6. Envio de datos propios a kernel memory 
    //    FORMATO: "MEMORYSTICK IP, MEMORYSTICK PORT"
    // -------------------------------------------------------------------

    // char msdatos[128];
    // snprintf(msdatos, sizeof(msdatos), "%d", ip);
    // snprintf(msdatos, sizeof(msdatos), ", %d", puerto);
    // uint32_t size;
    // void* payload = serializar_string(msdatos, &size);
    // enviar_mensaje(fd, MSG_MEMORY_STICK_IDENTIFICACION, payload, size);
    // free(payload);
    // Abrir el servidor antes de publicarlo en Kernel Memory: así nunca se
    // anuncia un endpoint al que una CPU todavía no puede conectarse.
    int fd_servidor = crear_servidor(puerto);
    if (fd_servidor < 0) {
        log_error(logger, "No se pudo levantar servidor en puerto %d", puerto);
        pthread_mutex_destroy(&memoria_global.mutex);
        free(memoria_global.buffer);
        config_destroy(config);
        log_destroy(logger);
        return EXIT_FAILURE;
    }

    // Conectarse a Kernel Memory
    int fd_km = conectar_a_servidor(kernel_ip, kernel_port);
    if (fd_km < 0) {
        log_error(logger, "No se pudo conectar a Kernel Memory en %s:%d", kernel_ip, kernel_port);
        close(fd_servidor);
        pthread_mutex_destroy(&memoria_global.mutex);
        free(memoria_global.buffer);
        config_destroy(config);
        log_destroy(logger);
        return EXIT_FAILURE;
    }

    // Publicar tamaño y puerto del servidor. KM obtiene la IP desde el peer de
    // esta conexión y luego puede resolver el endpoint para CPUs dinámicas.
    t_payload_memory_stick_identificacion payload = {
        .tamanio = htonl(tamanio),
        .puerto = htonl((uint32_t)puerto)
    };
    enviar_mensaje(fd_km, MSG_MEMORY_STICK_IDENTIFICACION, &payload, sizeof(payload));

    t_mensaje* respuesta = recibir_mensaje(fd_km);
    if (respuesta == NULL || respuesta->op_code != MSG_OK) {
        log_error(logger, "Kernel Memory rechazó la conexión");
        if (respuesta) free_mensaje(respuesta);
        close(fd_km);
        close(fd_servidor);
        pthread_mutex_destroy(&memoria_global.mutex);
        free(memoria_global.buffer);
        config_destroy(config);
        log_destroy(logger);
        return EXIT_FAILURE;
    }
    if (respuesta->payload_size >= sizeof(uint32_t)) {
        uint32_t offset_n;
        memcpy(&offset_n, respuesta->payload, sizeof(uint32_t));
        offset_global_ms = ntohl(offset_n);
    }
    free_mensaje(respuesta);

    log_info(logger, "## Conectado a Kernel Memory");
    log_debug(logger, "Offset global de este stick dentro del espacio de direcciones: %u", offset_global_ms);

    // NOTA [fix]: KM usa esta conexión para leer/escribir memoria física
    // (compactación, STDOUT/STDIN, suspensión). Antes nadie leía fd_km después
    // del handshake, así que todos esos pedidos quedaban sin respuesta y KM se
    // colgaba esperando. Este hilo atiende los pedidos de KM.
    int* fd_km_heap = malloc(sizeof(int));
    if (fd_km_heap == NULL) {
        log_error(logger, "No se pudo reservar memoria para atender Kernel Memory");
        close(fd_km);
        close(fd_servidor);
        pthread_mutex_destroy(&memoria_global.mutex);
        free(memoria_global.buffer);
        config_destroy(config);
        log_destroy(logger);
        return EXIT_FAILURE;
    }
    *fd_km_heap = fd_km;
    pthread_t t_km;
    if (pthread_create(&t_km, NULL, atender_kernel_memory, fd_km_heap) != 0) {
        log_error(logger, "No se pudo crear el hilo de Kernel Memory");
        free(fd_km_heap);
        close(fd_km);
        close(fd_servidor);
        pthread_mutex_destroy(&memoria_global.mutex);
        free(memoria_global.buffer);
        config_destroy(config);
        log_destroy(logger);
        return EXIT_FAILURE;
    }
    pthread_detach(t_km);

    // -------------------------------------------------------------------
    // 7. Creo servidor y se queda la espera de una CPU
    // -------------------------------------------------------------------

    log_info(logger, "Escuchando CPUs en puerto %d", puerto);

    while (1) {
        int fd_cpu = aceptar_conexion(fd_servidor);
        if (fd_cpu < 0) {
            log_warning(logger, "Error al aceptar conexión de CPU");
            continue;
        }

        t_cpu_args* args = malloc(sizeof(t_cpu_args));
        if (args == NULL) {
            log_error(logger, "Sin memoria para atender una conexión de CPU");
            close(fd_cpu);
            continue;
        }

        args->fd_cpu = fd_cpu;

        pthread_t tid;
        if (pthread_create(&tid, NULL, atender_cpu, args) != 0) {
            log_error(logger, "No se pudo crear el hilo para una CPU");
            free(args);
            close(fd_cpu);
            continue;
        }

        pthread_detach(tid);

        // TODO Check 2: loop de lectura/escritura por CPU
    }

    config_destroy(config);
    log_destroy(logger);
    return EXIT_SUCCESS;
}

// Atiende los pedidos de lectura/escritura que el Kernel Memory hace por la
// conexión persistente establecida al identificarse (modo pedido/respuesta).
void* atender_kernel_memory(void* arg) {
    int fd_km = *((int*)arg);
    free(arg);

    while (1) {
        t_mensaje* msg = recibir_mensaje(fd_km);
        if (msg == NULL) {
            log_warning(logger, "Kernel Memory cerró la conexión");
            break;
        }

        switch (msg->op_code) {
            case MSG_MEMORY_READ:
                // Conexión dedicada de KM: KM ya traduce global->local de su
                // lado antes de pedir (ms_para_direccion), así que la
                // dirección que manda acá ya es local a este stick.
                manejar_read(fd_km, msg, false);
                break;
            case MSG_MEMORY_WRITE:
                manejar_write(fd_km, msg, false);
                break;
            default:
                log_warning(logger, "KM: opcode inesperado %u", msg->op_code);
                enviar_mensaje(fd_km, MSG_ERROR, NULL, 0);
                break;
        }

        free_mensaje(msg);
    }
    close(fd_km);
    return NULL;
}

void* atender_cpu(void* arg) {
    t_cpu_args* args = (t_cpu_args*) arg;
    int fd_cpu = args->fd_cpu;
    free(args);

    // Bug A — identificación de la CPU
    t_mensaje* id_msg = recibir_mensaje(fd_cpu);
    if (id_msg == NULL) {
        log_info(logger, "CPU desconectada antes de identificarse");
        close(fd_cpu);
        return NULL;
    }
    if (id_msg->op_code != MSG_CPU_IDENTIFICACION ||
        id_msg->payload == NULL || id_msg->payload_size != sizeof(uint32_t)) {
        log_info(logger, "Primer mensaje no es identificacion: %u", id_msg->op_code);
        enviar_mensaje(fd_cpu, MSG_ERROR, NULL, 0);
        free_mensaje(id_msg);
        close(fd_cpu);
        return NULL;
    }

    uint32_t cpu_id_n;
    memcpy(&cpu_id_n, id_msg->payload, 4);
    uint32_t cpu_id = ntohl(cpu_id_n);
    free_mensaje(id_msg);

    log_info(logger, "## CPU %u Conectada", cpu_id);
    enviar_mensaje(fd_cpu, MSG_OK, NULL, 0);

    // Bug B + Bug C — loop por múltiples mensajes con op_codes correctos
    while (1) {
        t_mensaje* msg = recibir_mensaje(fd_cpu);
        if (msg == NULL) {
            log_info(logger, "CPU %u desconectada", cpu_id);
            break;
        }

        // Esta conexión es siempre CPU (KM habla por su propia conexión,
        // atendida en atender_kernel_memory) — toda dirección que llega acá
        // es global y hay que traducirla a local antes de usarla.
        switch (msg->op_code) {
            case MSG_LEER_MEMORIA:      // CPU usa este op_code (36)
                manejar_read_cpu(fd_cpu, msg);
                break;
            case MSG_ESCRIBIR_MEMORIA:  // CPU usa este op_code (38)
                manejar_write(fd_cpu, msg, true);  // responde MSG_OK — está bien
                break;
            case MSG_MEMORY_READ:       // CPU usa realmente este op_code (30) en la práctica
                manejar_read(fd_cpu, msg, true);
                break;
            case MSG_MEMORY_WRITE:      // CPU usa realmente este op_code (29) en la práctica
                manejar_write(fd_cpu, msg, true);
                break;
            default:
                log_info(logger, "CPU %u: opcode desconocido %u", cpu_id, msg->op_code);
                enviar_mensaje(fd_cpu, MSG_ERROR, NULL, 0);
                break;
        }

        free_mensaje(msg);
    }

    close(fd_cpu);
    return NULL;
}

void manejar_write(int fd_cpu, t_mensaje* msg, bool direccion_global) {
    if (msg == NULL || msg->payload == NULL || msg->payload_size < 8u) {
        log_warning(logger, "Pedido de escritura con payload incompleto");
        enviar_mensaje(fd_cpu, MSG_ERROR, NULL, 0);
        return;
    }

    uint32_t direccion_n;
    uint32_t size_n;
    memcpy(&direccion_n, msg->payload, 4);
    memcpy(&size_n, (uint8_t*)msg->payload + 4, 4);

    uint32_t direccion_recibida = ntohl(direccion_n);
    uint32_t size = ntohl(size_n);
    if (size > UINT32_MAX - 8u || msg->payload_size != 8u + size) {
        log_warning(logger, "Pedido de escritura inválido: tamaño declarado=%u payload=%u",
                    size, msg->payload_size);
        enviar_mensaje(fd_cpu, MSG_ERROR, NULL, 0);
        return;
    }

    uint32_t direccion_local;
    if (!resolver_rango(direccion_recibida, size, direccion_global, &direccion_local)) {
        log_warning(logger, "Escritura fuera de rango: dirección=%u tamaño=%u",
                    direccion_recibida, size);
        enviar_mensaje(fd_cpu, MSG_ERROR, NULL, 0);
        return;
    }

    aplicar_memory_delay();

    const uint8_t* datos = (const uint8_t*)msg->payload + 8;
    pthread_mutex_lock(&memoria_global.mutex);
    memcpy((uint8_t*)memoria_global.buffer + direccion_local, datos, size);
    pthread_mutex_unlock(&memoria_global.mutex);

    log_debug(logger, "Escritura en dirección local=%u (tamaño propio del stick=%u)",
              direccion_local, memoria_global.tamanio);
    log_info(logger, "## Escritura de %u bytes", size);
    enviar_mensaje(fd_cpu, MSG_OK, NULL, 0);
}

static void manejar_read_con_respuesta(int fd, t_mensaje* msg, bool direccion_global,
                                       uint32_t op_respuesta) {
    if (msg == NULL || msg->payload == NULL || msg->payload_size != 8u) {
        log_warning(logger, "Pedido de lectura con payload inválido");
        enviar_mensaje(fd, MSG_ERROR, NULL, 0);
        return;
    }

    uint32_t direccion_n;
    uint32_t size_n;
    memcpy(&direccion_n, msg->payload, 4);
    memcpy(&size_n, (uint8_t*)msg->payload + 4, 4);

    uint32_t direccion_recibida = ntohl(direccion_n);
    uint32_t size = ntohl(size_n);
    uint32_t direccion_local;
    if (!resolver_rango(direccion_recibida, size, direccion_global, &direccion_local)) {
        log_warning(logger, "Lectura fuera de rango: dirección=%u tamaño=%u",
                    direccion_recibida, size);
        enviar_mensaje(fd, MSG_ERROR, NULL, 0);
        return;
    }

    uint8_t* buffer = malloc(size);
    if (buffer == NULL) {
        log_error(logger, "Sin memoria para responder una lectura de %u bytes", size);
        enviar_mensaje(fd, MSG_ERROR, NULL, 0);
        return;
    }

    aplicar_memory_delay();

    pthread_mutex_lock(&memoria_global.mutex);
    memcpy(buffer, (uint8_t*)memoria_global.buffer + direccion_local, size);
    pthread_mutex_unlock(&memoria_global.mutex);

    log_debug(logger, "Lectura en dirección local=%u (tamaño propio del stick=%u)",
              direccion_local, memoria_global.tamanio);
    log_info(logger, "## Lectura de %u bytes", size);
    enviar_mensaje(fd, op_respuesta, buffer, size);
    free(buffer);
}

void manejar_read(int fd_cpu, t_mensaje* msg, bool direccion_global) {
    manejar_read_con_respuesta(fd_cpu, msg, direccion_global, MSG_MEMORY_READ_RESPUESTA);
}

void manejar_read_cpu(int fd_cpu, t_mensaje* msg) {
    manejar_read_con_respuesta(fd_cpu, msg, true, MSG_LEER_MEMORIA_RESP);
}
