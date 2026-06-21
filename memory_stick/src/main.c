#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <commons/log.h>
#include <commons/config.h>
#include <utils/sockets.h>
#include <utils/protocolo.h>
#include <pthread.h>
#include <unistd.h>

// Ver si es necesario, para facilitar manejos desde cpus
// int obtenerId();

void* atender_cpu(void* arg);
void manejar_write(int fd_cpu, t_mensaje* msg);
void manejar_read(int fd_cpu, t_mensaje* msg);
void manejar_read_cpu(int fd_cpu, t_mensaje* msg);

int delay;

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

int id = 1;

int main(int argc, char* argv[]) {
    // -------------------------------------------------------------------
    // 0. generar id de memorystick
    // -------------------------------------------------------------------

    // id = obtenerId();
    // log_info(logger, "id: %d", id);

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


    // char * ip         = config_get_string_value(config,    "MEMORY_STICK_IP");
    int   puerto      = config_get_int_value(config,    "MEMORY_STICK_PORT");
          delay       = config_get_int_value(config,    "MEMORY_DELAY");
    int   kernel_port = config_get_int_value(config,    "KERNEL_MEMORY_PORT");
    char* kernel_ip   = config_get_string_value(config, "KERNEL_MEMORY_IP");
    char* logLevel    = config_get_string_value(config, "LOG_LEVEL");
    // -------------------------------------------------------------------
    // 3. Inicializar logger
    // -------------------------------------------------------------------

    char log_file[64];
    snprintf(log_file, sizeof(log_file), "memory_stick_%d.log", id);

    logger = log_create(log_file, "MemoryStick", true, log_level_from_string(logLevel));
    if (logger == NULL) {
        fprintf(stderr, "Error al crear el logger\n");
        config_destroy(config);
        return EXIT_FAILURE;
    }
    // -------------------------------------------------------------------
    // 4. Conectarse al Kernel Memory
    // -------------------------------------------------------------------

    if (kernel_ip == NULL) {
        log_error(logger, "Falta KERNEL_MEMORY_IP en el archivo de configuración");
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
    // Conectarse a Kernel Memory
    int fd_km = conectar_a_servidor(kernel_ip, kernel_port);
    if (fd_km < 0) {
        log_error(logger, "No se pudo conectar a Kernel Memory en %s:%d", kernel_ip, kernel_port);
        config_destroy(config);
        log_destroy(logger);
        return EXIT_FAILURE;
    }

    // Identificarse con tamaño en el payload
    //  4 + 4 + 32
    // u_int64_t  payload[40];
    // uint32_t ip_n = htonl(ms.ip);
    // memcpy(payload, &ip_n, 32);
    // uint32_t puerto_n = htonl(ms.puerto);
    // memcpy(payload, &puerto_n, 4);
    // uint32_t tamanio_n = htonl(tamanio);
    // memcpy(payload, &tamanio_n, 4);
    uint8_t  payload[4];
    uint32_t tamanio_n = htonl(tamanio);
    memcpy(payload, &tamanio_n, 4);

    enviar_mensaje(fd_km, MSG_MEMORY_STICK_IDENTIFICACION, payload, sizeof(payload));

    t_mensaje* respuesta = recibir_mensaje(fd_km);
    if (respuesta == NULL || respuesta->op_code != MSG_OK) {
        log_error(logger, "Kernel Memory rechazó la conexión");
        if (respuesta) free_mensaje(respuesta);
        config_destroy(config);
        log_destroy(logger);
        return EXIT_FAILURE;
    }
    free_mensaje(respuesta);

    log_info(logger, "## Conectado a Kernel Memory");

    // -------------------------------------------------------------------
    // 7. Creo servidor y se queda la espera de una CPU
    // -------------------------------------------------------------------

    // Levantar servidor para CPUs
    int fd_servidor = crear_servidor(puerto);
    if (fd_servidor < 0) {
        log_error(logger, "No se pudo levantar servidor en puerto %d", puerto);
        config_destroy(config);
        log_destroy(logger);
        return EXIT_FAILURE;
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

        // TODO Check 2: loop de lectura/escritura por CPU
    }

    config_destroy(config);
    log_destroy(logger);
    return EXIT_SUCCESS;
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
    if (id_msg->op_code != MSG_CPU_IDENTIFICACION) {
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

        switch (msg->op_code) {
            case MSG_LEER_MEMORIA:      // CPU usa este op_code (36)
                manejar_read_cpu(fd_cpu, msg);
                break;
            case MSG_ESCRIBIR_MEMORIA:  // CPU usa este op_code (38)
                manejar_write(fd_cpu, msg);  // responde MSG_OK — está bien
                break;
            case MSG_MEMORY_READ:       // KM usa este op_code (30) — mantener compatibilidad
                manejar_read(fd_cpu, msg);
                break;
            case MSG_MEMORY_WRITE:      // KM usa este op_code (29) — mantener compatibilidad
                manejar_write(fd_cpu, msg);
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

void manejar_write(int fd_cpu, t_mensaje* msg) {

    uint32_t direccion_n;
    uint32_t size_n;

    memcpy(&direccion_n,msg->payload,4);

    memcpy(&size_n,msg->payload + 4,4);

    uint32_t direccion = ntohl(direccion_n);

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

    log_info(logger,"## Escritura de %u bytes",size);

    enviar_mensaje(fd_cpu,MSG_OK,NULL,0);
}

void manejar_read(int fd_cpu, t_mensaje* msg) {

    uint32_t direccion_n;
    uint32_t size_n;

    memcpy(&direccion_n,msg->payload,4);

    memcpy(&size_n,msg->payload + 4,4);

    uint32_t direccion = ntohl(direccion_n);

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

    log_info(logger, "## Lectura de %u bytes", size);

    enviar_mensaje(fd_cpu, MSG_MEMORY_READ_RESPUESTA, buffer, size);

    free(buffer);
}

void manejar_read_cpu(int fd_cpu, t_mensaje* msg) {
    uint32_t direccion_n;
    uint32_t size_n;

    memcpy(&direccion_n, msg->payload, 4);
    memcpy(&size_n, (uint8_t*)msg->payload + 4, 4);

    uint32_t direccion = ntohl(direccion_n);
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

    log_info(logger, "## Lectura de %u bytes", size);

    enviar_mensaje(fd_cpu, MSG_LEER_MEMORIA_RESP, buffer, size);  // ← op_code correcto para CPU

    free(buffer);
}

