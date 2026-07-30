#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <commons/log.h>
#include <commons/config.h>
#include <utils/sockets.h>
#include <utils/protocolo.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

void* atender_cpu(void* arg);
void* atender_kernel_memory(void* arg);
// direccion_global: true si la dirección que llega es global (viene directo
// de la CPU, vía MOV_IN/MOV_OUT/COPY_MEM) y hay que restarle offset_global_ms
// antes de indexar el buffer propio; false si ya viene local (KM, que ya hace
// la traducción global->local del lado de kernel_memory antes de pedir).
void manejar_write(int fd_cpu, t_mensaje* msg, bool direccion_global);
void manejar_read(int fd_cpu, t_mensaje* msg, bool direccion_global);
void manejar_read_cpu(int fd_cpu, t_mensaje* msg);

int delay;

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

int main(int argc, char* argv[]) {
    // -------------------------------------------------------------------
    // 1. Validar argumentos
    // -------------------------------------------------------------------


    if (argc < 3) {
        fprintf(stderr, "Uso: %s [Archivo Config] [Tamaño]\n", argv[0]);
        return EXIT_FAILURE;
    }

    char*    config_path = argv[1];
    uint32_t tamanio     = (uint32_t)atoi(argv[2]);
    if (tamanio == 0) {
    fprintf(stderr, "Tamaño inválido\n");
    return EXIT_FAILURE;
    }

    // inicialización del espacio de almacenamiento
    memoria_global.buffer = malloc(tamanio);

    if (memoria_global.buffer == NULL) {

        fprintf(stderr, "No se pudo reservar memoria\n");

        return EXIT_FAILURE;
    }

    memoria_global.tamanio = tamanio;

    pthread_mutex_init(&memoria_global.mutex,NULL);

    // -------------------------------------------------------------------
    // 2. Leer configuración y cargar variables
    // -------------------------------------------------------------------

    t_config* config = config_create(config_path);
    if (config == NULL) {
        fprintf(stderr, "No se pudo leer el archivo de configuración: %s\n", config_path);
        return EXIT_FAILURE;
    }


    delay           = config_get_int_value(config, "MEMORY_DELAY");
    int   kernel_port = config_get_int_value(config,    "KERNEL_MEMORY_PORT");
    char* kernel_ip   = config_get_string_value(config, "KERNEL_MEMORY_IP");
    char* logLevel    = config_get_string_value(config, "LOG_LEVEL");

    // El servidor debe estar escuchando antes de publicar el stick en KM.
    // bind(0) elige un puerto libre y permite reutilizar el mismo config.
    int fd_servidor = crear_servidor(0);
    if (fd_servidor < 0) {
        fprintf(stderr, "No se pudo levantar el servidor de Memory Stick\n");
        config_destroy(config);
        return EXIT_FAILURE;
    }

    struct sockaddr_in servidor_addr;
    socklen_t servidor_addr_len = sizeof(servidor_addr);
    if (getsockname(fd_servidor, (struct sockaddr*)&servidor_addr, &servidor_addr_len) < 0) {
        fprintf(stderr, "No se pudo obtener el puerto del Memory Stick\n");
        close(fd_servidor);
        config_destroy(config);
        return EXIT_FAILURE;
    }
    int puerto = ntohs(servidor_addr.sin_port);
    // -------------------------------------------------------------------
    // 3. Inicializar logger
    // -------------------------------------------------------------------

    // El puerto efectivo es único aunque varias instancias compartan config.
    char log_file[64];
    snprintf(log_file, sizeof(log_file), "memory_stick_%d.log", puerto);

    logger = log_create(log_file, "MemoryStick", true, log_level_from_string(logLevel));
    if (logger == NULL) {
        fprintf(stderr, "Error al crear el logger\n");
        close(fd_servidor);
        config_destroy(config);
        return EXIT_FAILURE;
    }

    log_info(logger, "Se creo log en memory_stick_%d.log", puerto);
    // -------------------------------------------------------------------
    // 4. Conectarse al Kernel Memory
    // -------------------------------------------------------------------

    if (kernel_ip == NULL) {
        log_error(logger, "Falta KERNEL_MEMORY_IP en el archivo de configuración");
        close(fd_servidor);
        config_destroy(config);
        log_destroy(logger);
        return EXIT_FAILURE;
    }

    int fd_km = conectar_a_servidor(kernel_ip, kernel_port);
    if (fd_km < 0) {
        log_error(logger, "No se pudo conectar a Kernel Memory en %s:%d", kernel_ip, kernel_port);
        close(fd_servidor);
        config_destroy(config);
        log_destroy(logger);
        return EXIT_FAILURE;
    }

    t_payload_memory_stick_identificacion identificacion = {
        .tamanio = htonl(tamanio),
        .puerto = htonl((uint32_t)puerto)
    };
    enviar_mensaje(fd_km, MSG_MEMORY_STICK_IDENTIFICACION,
                   &identificacion, sizeof(identificacion));

    t_mensaje* respuesta = recibir_mensaje(fd_km);
    if (respuesta == NULL || respuesta->op_code != MSG_OK ||
        respuesta->payload_size != sizeof(uint32_t)) {
        log_error(logger, "Kernel Memory rechazó la conexión");
        if (respuesta) free_mensaje(respuesta);
        close(fd_km);
        close(fd_servidor);
        config_destroy(config);
        log_destroy(logger);
        return EXIT_FAILURE;
    }
    uint32_t offset_n;
    memcpy(&offset_n, respuesta->payload, sizeof(uint32_t));
    offset_global_ms = ntohl(offset_n);
    free_mensaje(respuesta);

    log_info(logger, "## Conectado a Kernel Memory");
    log_debug(logger, "Offset global de este stick dentro del espacio de direcciones: %u", offset_global_ms);

    // NOTA [fix]: KM usa esta conexión para leer/escribir memoria física
    // (compactación, STDOUT/STDIN, suspensión). Antes nadie leía fd_km después
    // del handshake, así que todos esos pedidos quedaban sin respuesta y KM se
    // colgaba esperando. Este hilo atiende los pedidos de KM.
    {
        int* fd_km_heap = malloc(sizeof(int));
        *fd_km_heap = fd_km;
        pthread_t t_km;
        pthread_create(&t_km, NULL, atender_kernel_memory, fd_km_heap);
        pthread_detach(t_km);
    }

    log_info(logger, "Escuchando CPUs en puerto %d", puerto);

    while (1) {
        int fd_cpu = aceptar_conexion(fd_servidor);
        if (fd_cpu < 0) {
            log_warning(logger, "Error al aceptar conexión de CPU");
            continue;
        }

        t_cpu_args* args = malloc(sizeof(t_cpu_args));

        args->fd_cpu = fd_cpu;

        pthread_t tid;

        pthread_create(&tid, NULL, atender_cpu, args);

        pthread_detach(tid);
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
        id_msg->payload_size != sizeof(uint32_t)) {
        log_info(logger, "Identificacion de CPU invalida: opcode=%u payload=%u",
                 id_msg->op_code, id_msg->payload_size);
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

    uint32_t direccion_n;
    uint32_t size_n;

    memcpy(&direccion_n,msg->payload,4);

    memcpy(&size_n,msg->payload + 4,4);

    uint32_t direccion = ntohl(direccion_n);
    if (direccion_global) direccion -= offset_global_ms;

    uint32_t size = ntohl(size_n);

    void* datos = msg->payload + 8;

    // ------------------------------------------------
    // Delay
    // ------------------------------------------------

    usleep(delay);

    // ------------------------------------------------
    // Validar límites
    // ------------------------------------------------

    if (direccion + size > memoria_global.tamanio) {

        enviar_mensaje(fd_cpu, MSG_ERROR, NULL, 0);
        log_info(logger, "La cantidad de bytes a escribir es mayor al tamaño disponible");
        return;
    }

    // ------------------------------------------------
    // Mutex
    // ------------------------------------------------

    pthread_mutex_lock(&memoria_global.mutex);

    memcpy((char*)memoria_global.buffer + direccion, datos, size);

    pthread_mutex_unlock(&memoria_global.mutex);

    log_debug(logger, "Escritura en dirección local=%u (tamaño propio del stick=%u)", direccion, memoria_global.tamanio);
    log_info(logger,"## Escritura de %u bytes",size);

    enviar_mensaje(fd_cpu,MSG_OK,NULL,0);
}

void manejar_read(int fd_cpu, t_mensaje* msg, bool direccion_global) {

    uint32_t direccion_n;
    uint32_t size_n;

    memcpy(&direccion_n,msg->payload,4);

    memcpy(&size_n,msg->payload + 4,4);

    uint32_t direccion = ntohl(direccion_n);
    if (direccion_global) direccion -= offset_global_ms;

    uint32_t size = ntohl(size_n);

    usleep(delay);

    if (direccion + size > memoria_global.tamanio) {
        enviar_mensaje(fd_cpu, MSG_ERROR, NULL, 0);
        log_info(logger, "La cantidad de bytes a leer es mayor al tamaño");
        return;
    }

    void* buffer = malloc(size);

    pthread_mutex_lock(&memoria_global.mutex);

    memcpy(buffer, (char*)memoria_global.buffer + direccion, size);

    pthread_mutex_unlock(&memoria_global.mutex);

    log_debug(logger, "Lectura en dirección local=%u (tamaño propio del stick=%u)", direccion, memoria_global.tamanio);
    log_info(logger, "## Lectura de %u bytes", size);

    enviar_mensaje(fd_cpu, MSG_MEMORY_READ_RESPUESTA, buffer, size);

    free(buffer);
}

void manejar_read_cpu(int fd_cpu, t_mensaje* msg) {
    uint32_t direccion_n;
    uint32_t size_n;

    memcpy(&direccion_n, msg->payload, 4);
    memcpy(&size_n, (uint8_t*)msg->payload + 4, 4);

    uint32_t direccion = ntohl(direccion_n) - offset_global_ms; // siempre CPU: dirección global
    uint32_t size      = ntohl(size_n);

    usleep(delay);

    if (direccion + size > memoria_global.tamanio) {
        enviar_mensaje(fd_cpu, MSG_ERROR, NULL, 0);
        log_info(logger, "La cantidad de bytes a leer es mayor al tamaño");
        return;
    }

    void* buffer = malloc(size);

    pthread_mutex_lock(&memoria_global.mutex);
    memcpy(buffer, (char*)memoria_global.buffer + direccion, size);
    pthread_mutex_unlock(&memoria_global.mutex);

    log_debug(logger, "Lectura en dirección local=%u (tamaño propio del stick=%u)", direccion, memoria_global.tamanio);
    log_info(logger, "## Lectura de %u bytes", size);

    enviar_mensaje(fd_cpu, MSG_LEER_MEMORIA_RESP, buffer, size);  // ← op_code correcto para CPU

    free(buffer);
}
