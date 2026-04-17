#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <stdint.h>
#include <signal.h>

#include "../../utils/src/utils/protocol.h"
#include "../../utils/src/utils/net_utils.h"

#define DEFAULT_PORT 8002
#define DEFAULT_LOG_LEVEL "INFO"
#define DEFAULT_SEGMENT_MAX 256
#define DEFAULT_ALLOC_STRATEGY "BEST"
#define DEFAULT_INSTRUCTION_DELAY 500
#define DEFAULT_COMPACTION_DELAY 30000
#define DEFAULT_SCRIPTS_BASEPATH "/tmp"

typedef struct {
    int port;
    char log_level[16];
    int segment_max_size;
    char allocation_strategy[8];
    int instruction_delay;
    int compaction_delay;
    char scripts_basepath[256];
} t_km_config;

static t_km_config g_config;
static int g_server_fd = -1;
static pthread_mutex_t g_log_mutex = PTHREAD_MUTEX_INITIALIZER;

#define LOG_INFO(fmt, ...) \
    do { \
        pthread_mutex_lock(&g_log_mutex); \
        printf("[INFO] " fmt "\n", ##__VA_ARGS__); \
        fflush(stdout); \
        pthread_mutex_unlock(&g_log_mutex); \
    } while (0)

#define LOG_DEBUG(fmt, ...) \
    do { \
        if (strcmp(g_config.log_level, "DEBUG") == 0) { \
            pthread_mutex_lock(&g_log_mutex); \
            printf("[DEBUG] " fmt "\n", ##__VA_ARGS__); \
            fflush(stdout); \
            pthread_mutex_unlock(&g_log_mutex); \
        } \
    } while (0)

#define LOG_ERROR(fmt, ...) \
    do { \
        pthread_mutex_lock(&g_log_mutex); \
        fprintf(stderr, "[ERROR] " fmt "\n", ##__VA_ARGS__); \
        fflush(stderr); \
        pthread_mutex_unlock(&g_log_mutex); \
    } while (0)

static void config_defaults(void) {
    g_config.port = DEFAULT_PORT;
    strncpy(g_config.log_level, DEFAULT_LOG_LEVEL, sizeof(g_config.log_level));
    g_config.segment_max_size = DEFAULT_SEGMENT_MAX;
    strncpy(g_config.allocation_strategy, DEFAULT_ALLOC_STRATEGY, sizeof(g_config.allocation_strategy));
    g_config.instruction_delay = DEFAULT_INSTRUCTION_DELAY;
    g_config.compaction_delay = DEFAULT_COMPACTION_DELAY;
    strncpy(g_config.scripts_basepath, DEFAULT_SCRIPTS_BASEPATH, sizeof(g_config.scripts_basepath));
}

static void config_load(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "No se pudo abrir config '%s', usando defaults.\n", path);
        return;
    }

    char line[512];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;

        char key[128], value[384];
        if (sscanf(line, "%127[^=]=%383[^\n]", key, value) != 2) continue;

        char *v = value;
        while (*v == ' ') v++;

        if (!strcmp(key, "PORT")) g_config.port = atoi(v);
        else if (!strcmp(key, "LOG_LEVEL")) strncpy(g_config.log_level, v, sizeof(g_config.log_level));
        else if (!strcmp(key, "SEGMENT_MAX_SIZE")) g_config.segment_max_size = atoi(v);
        else if (!strcmp(key, "ALLOCATION_STRATEGY")) strncpy(g_config.allocation_strategy, v, sizeof(g_config.allocation_strategy));
        else if (!strcmp(key, "INSTRUCTION_DELAY")) g_config.instruction_delay = atoi(v);
        else if (!strcmp(key, "COMPACTION_DELAY")) g_config.compaction_delay = atoi(v);
        else if (!strcmp(key, "SCRIPTS_BASEPATH")) strncpy(g_config.scripts_basepath, v, sizeof(g_config.scripts_basepath));
    }

    fclose(f);
}

typedef struct {
    int client_fd;
    struct sockaddr_in client_addr;
} t_client_arg;

static void handle_kernel_scheduler(int fd) {
    LOG_INFO("Kernel Scheduler conectado (fd=%d)", fd);

    if (send_signal(fd, MSG_HANDSHAKE_ACK) < 0) {
        LOG_ERROR("Error enviando ACK a Kernel Scheduler (fd=%d)", fd);
        close(fd);
        return;
    }

    LOG_DEBUG("Esperando mensajes de Kernel Scheduler (fd=%d)", fd);

    uint8_t tipo;
    uint32_t size;

    while (1) {
        void *payload = recv_msg(fd, &tipo, &size);
        if (!payload) {
            LOG_INFO("Kernel Scheduler desconectado (fd=%d)", fd);
            break;
        }

        if (size > 0) free(payload);

        LOG_DEBUG("Mensaje KS tipo=0x%02X size=%u (fd=%d)", tipo, size, fd);
        send_signal(fd, MSG_OK);
    }

    close(fd);
}

static void handle_cpu(int fd) {
    uint8_t tipo;
    uint32_t size;
    void *payload = recv_msg(fd, &tipo, &size);

    if (!payload || tipo != MSG_HANDSHAKE_CPU || size < sizeof(uint32_t)) {
        LOG_ERROR("Handshake CPU inválido (fd=%d)", fd);
        if (payload && size > 0) free(payload);
        close(fd);
        return;
    }

    uint32_t cpu_id;
    memcpy(&cpu_id, payload, sizeof(uint32_t));
    if (size > 0) free(payload);

    LOG_INFO("CPU %u conectada", cpu_id);

    if (send_signal(fd, MSG_HANDSHAKE_ACK) < 0) {
        LOG_ERROR("Error enviando ACK a CPU %u (fd=%d)", cpu_id, fd);
        close(fd);
        return;
    }

    while (1) {
        payload = recv_msg(fd, &tipo, &size);
        if (!payload) {
            LOG_INFO("CPU %u desconectada (fd=%d)", cpu_id, fd);
            break;
        }

        if (size > 0) free(payload);

        LOG_DEBUG("CPU %u msg tipo=0x%02X size=%u", cpu_id, tipo, size);
        send_signal(fd, MSG_OK);
    }

    close(fd);
}

static void handle_memory_stick(int fd) {
    uint8_t tipo;
    uint32_t size;
    void *payload = recv_msg(fd, &tipo, &size);

    if (!payload || tipo != MSG_HANDSHAKE_MS || size < sizeof(uint32_t)) {
        LOG_ERROR("Handshake Memory Stick inválido (fd=%d)", fd);
        if (payload && size > 0) free(payload);
        close(fd);
        return;
    }

    uint32_t ms_size;
    memcpy(&ms_size, payload, sizeof(uint32_t));
    if (size > 0) free(payload);

    LOG_INFO("Memory Stick conectada (%u bytes)", ms_size);

    if (send_signal(fd, MSG_HANDSHAKE_ACK) < 0) {
        LOG_ERROR("Error enviando ACK a Memory Stick (fd=%d)", fd);
        close(fd);
        return;
    }

    while (1) {
        payload = recv_msg(fd, &tipo, &size);
        if (!payload) {
            LOG_INFO("Memory Stick desconectada (fd=%d)", fd);
            break;
        }

        if (size > 0) free(payload);

        LOG_DEBUG("Memory Stick msg tipo=0x%02X (fd=%d)", tipo, fd);
        send_signal(fd, MSG_OK);
    }

    close(fd);
}

static void handle_swap(int fd) {
    uint8_t tipo;
    uint32_t size;
    void *payload = recv_msg(fd, &tipo, &size);

    if (!payload || tipo != MSG_HANDSHAKE_SWAP || size < 2 * sizeof(uint32_t)) {
        LOG_ERROR("Handshake SWAP inválido (fd=%d)", fd);
        if (payload && size > 0) free(payload);
        close(fd);
        return;
    }

    uint32_t block_size, total_size;
    memcpy(&block_size, payload, sizeof(uint32_t));
    memcpy(&total_size, (uint8_t *)payload + sizeof(uint32_t), sizeof(uint32_t));
    if (size > 0) free(payload);

    LOG_INFO("SWAP conectado (bloque=%u total=%u)", block_size, total_size);

    if (send_signal(fd, MSG_HANDSHAKE_ACK) < 0) {
        LOG_ERROR("Error enviando ACK a SWAP (fd=%d)", fd);
        close(fd);
        return;
    }

    while (1) {
        payload = recv_msg(fd, &tipo, &size);
        if (!payload) {
            LOG_INFO("SWAP desconectado (fd=%d)", fd);
            break;
        }

        if (size > 0) free(payload);

        LOG_DEBUG("SWAP msg tipo=0x%02X", tipo);
        send_signal(fd, MSG_OK);
    }

    close(fd);
}

static void *client_thread(void *arg) {
    t_client_arg *carg = (t_client_arg *)arg;
    int fd = carg->client_fd;
    free(carg);

    pthread_detach(pthread_self());

    uint8_t client_type;
    if (recv(fd, &client_type, sizeof(client_type), MSG_WAITALL) <= 0) {
        LOG_ERROR("No se pudo leer tipo de cliente (fd=%d)", fd);
        close(fd);
        return NULL;
    }

    switch ((t_client_type)client_type) {
        case CLIENT_KERNEL_SCHEDULER: handle_kernel_scheduler(fd); break;
        case CLIENT_CPU: handle_cpu(fd); break;
        case CLIENT_MEMORY_STICK: handle_memory_stick(fd); break;
        case CLIENT_SWAP: handle_swap(fd); break;
        default:
            LOG_ERROR("Cliente desconocido: 0x%02X (fd=%d)", client_type, fd);
            close(fd);
            break;
    }

    return NULL;
}

static void accept_loop(void) {
    LOG_INFO("Escuchando en puerto %d", g_config.port);

    while (1) {
        t_client_arg *arg = malloc(sizeof(t_client_arg));
        if (!arg) {
            LOG_ERROR("malloc fallido");
            continue;
        }

        socklen_t len = sizeof(arg->client_addr);
        arg->client_fd = accept(g_server_fd, (struct sockaddr *)&arg->client_addr, &len);

        if (arg->client_fd < 0) {
            free(arg);
            if (errno == EINTR) continue;
            if (errno == EBADF) break;
            perror("accept");
            continue;
        }

        pthread_t tid;
        if (pthread_create(&tid, NULL, client_thread, arg) != 0) {
            LOG_ERROR("Error creando hilo (fd=%d)", arg->client_fd);
            close(arg->client_fd);
            free(arg);
        }
    }
}

static void sighandler(int sig) {
    (void)sig;
    printf("\n[INFO] Cerrando...\n");
    if (g_server_fd >= 0) close(g_server_fd);
    exit(0);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Uso: %s [config]\n", argv[0]);
        return EXIT_FAILURE;
    }

    config_defaults();
    config_load(argv[1]);

    printf("[INFO] Iniciando\n");
    printf("[INFO] Log level: %s\n", g_config.log_level);
    printf("[INFO] Puerto: %d\n", g_config.port);
    printf("[INFO] Segment max: %d\n", g_config.segment_max_size);
    printf("[INFO] Strategy: %s\n", g_config.allocation_strategy);
    printf("[INFO] Basepath: %s\n", g_config.scripts_basepath);

    signal(SIGINT, sighandler);
    signal(SIGTERM, sighandler);
    signal(SIGPIPE, SIG_IGN);

    g_server_fd = create_server(g_config.port);
    if (g_server_fd < 0) {
        fprintf(stderr, "No se pudo iniciar servidor\n");
        return EXIT_FAILURE;
    }

    accept_loop();

    close(g_server_fd);
    return EXIT_SUCCESS;
}
