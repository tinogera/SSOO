#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <pthread.h>
#include <signal.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "../../utils/src/utils/protocol.h"
#include "../../utils/src/utils/net_utils.h"

#define DEFAULT_LOG_LEVEL "INFO"
#define DEFAULT_KM_IP "127.0.0.1"
#define DEFAULT_KM_PORT 8002
#define DEFAULT_CPU_PORT 8004
#define DEFAULT_MEMORY_DELAY 1500

typedef struct {
    char log_level[16];
    char km_ip[64];
    int km_port;
    int cpu_listen_port;
    int memory_delay;
} t_ms_config;

static t_ms_config g_config;
static int g_km_fd = -1;
static int g_server_fd = -1;
static uint8_t *g_memory = NULL;
static uint32_t g_mem_size = 0;

static pthread_mutex_t g_mem_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_log_mutex = PTHREAD_MUTEX_INITIALIZER;

#define LOG_INFO(fmt, ...) \
    do { pthread_mutex_lock(&g_log_mutex); \
         printf("[INFO] " fmt "\n", ##__VA_ARGS__); fflush(stdout); \
         pthread_mutex_unlock(&g_log_mutex); } while(0)

#define LOG_DEBUG(fmt, ...) \
    do { if (!strcmp(g_config.log_level,"DEBUG")) { \
         pthread_mutex_lock(&g_log_mutex); \
         printf("[DEBUG] " fmt "\n", ##__VA_ARGS__); fflush(stdout); \
         pthread_mutex_unlock(&g_log_mutex); } } while(0)

#define LOG_ERROR(fmt, ...) \
    do { pthread_mutex_lock(&g_log_mutex); \
         fprintf(stderr,"[ERROR] " fmt "\n", ##__VA_ARGS__); fflush(stderr); \
         pthread_mutex_unlock(&g_log_mutex); } while(0)

static void config_defaults(void) {
    strncpy(g_config.log_level, DEFAULT_LOG_LEVEL, sizeof(g_config.log_level));
    strncpy(g_config.km_ip, DEFAULT_KM_IP, sizeof(g_config.km_ip));
    g_config.km_port = DEFAULT_KM_PORT;
    g_config.cpu_listen_port = DEFAULT_CPU_PORT;
    g_config.memory_delay = DEFAULT_MEMORY_DELAY;
}

static void config_load(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return;

    char line[512], key[128], value[384];

    while (fgets(line, sizeof(line), f)) {
        if (line[0]=='#'||line[0]=='\n'||line[0]=='\r') continue;
        if (sscanf(line,"%127[^=]=%383[^\n]",key,value)!=2) continue;

        char *v = value;
        while (*v==' ') v++;

        if (!strcmp(key,"LOG_LEVEL")) strncpy(g_config.log_level,v,sizeof(g_config.log_level));
        else if (!strcmp(key,"KM_IP")) strncpy(g_config.km_ip,v,sizeof(g_config.km_ip));
        else if (!strcmp(key,"KM_PORT")) g_config.km_port = atoi(v);
        else if (!strcmp(key,"CPU_LISTEN_PORT")) g_config.cpu_listen_port = atoi(v);
        else if (!strcmp(key,"MEMORY_DELAY")) g_config.memory_delay = atoi(v);
    }

    fclose(f);
}

static void sighandler(int sig) {
    (void)sig;
    printf("\n[INFO] Cerrando...\n");

    if (g_km_fd >= 0) close(g_km_fd);
    if (g_server_fd >= 0) close(g_server_fd);

    free(g_memory);
    exit(0);
}

static int handshake_with_km(void) {
    uint8_t ctype = CLIENT_MEMORY_STICK;

    if (send(g_km_fd, &ctype, sizeof(ctype), MSG_NOSIGNAL) < 0)
        return -1;

    uint32_t sz = g_mem_size;

    if (send_msg(g_km_fd, MSG_HANDSHAKE_MS, &sz, sizeof(sz)) < 0)
        return -1;

    uint8_t tipo;
    uint32_t size;

    void *resp = recv_msg(g_km_fd, &tipo, &size);

    if (!resp || tipo != MSG_HANDSHAKE_ACK) {
        if (resp && size > 0) free(resp);
        return -1;
    }

    return 0;
}

typedef struct { int fd; } t_cpu_arg;

static void *cpu_thread(void *arg) {
    t_cpu_arg *carg = arg;
    int fd = carg->fd;
    free(carg);

    pthread_detach(pthread_self());

    uint8_t ctype;
    if (recv(fd,&ctype,sizeof(ctype),MSG_WAITALL)<=0 || ctype!=CLIENT_CPU) {
        close(fd);
        return NULL;
    }

    uint8_t tipo;
    uint32_t size;

    void *payload = recv_msg(fd,&tipo,&size);
    if (!payload || tipo!=MSG_HANDSHAKE_CPU || size<sizeof(uint32_t)) {
        if (payload && size>0) free(payload);
        close(fd);
        return NULL;
    }

    uint32_t cpu_id;
    memcpy(&cpu_id,payload,sizeof(uint32_t));
    free(payload);

    LOG_INFO("CPU %u conectada", cpu_id);

    send_signal(fd,MSG_HANDSHAKE_ACK);

    while (1) {
        payload = recv_msg(fd,&tipo,&size);
        if (!payload) break;

        usleep((useconds_t)g_config.memory_delay * 1000);

        if (tipo == MSG_MS_READ) {
            if (size < 8) {
                if (size>0) free(payload);
                send_signal(fd,MSG_ERROR);
                continue;
            }

            uint32_t dir, cant;
            memcpy(&dir,payload,4);
            memcpy(&cant,(uint8_t*)payload+4,4);
            free(payload);

            pthread_mutex_lock(&g_mem_mutex);

            if (dir+cant > g_mem_size) {
                pthread_mutex_unlock(&g_mem_mutex);
                send_signal(fd,MSG_ERROR);
                continue;
            }

            uint8_t *buf = malloc(cant);
            memcpy(buf,g_memory+dir,cant);

            pthread_mutex_unlock(&g_mem_mutex);

            send_msg(fd,MSG_OK,buf,cant);
            free(buf);
        }
        else if (tipo == MSG_MS_WRITE) {
            if (size < 8) {
                if (size>0) free(payload);
                send_signal(fd,MSG_ERROR);
                continue;
            }

            uint32_t dir, cant;
            memcpy(&dir,payload,4);
            memcpy(&cant,(uint8_t*)payload+4,4);

            pthread_mutex_lock(&g_mem_mutex);

            if (dir+cant > g_mem_size) {
                pthread_mutex_unlock(&g_mem_mutex);
                free(payload);
                send_signal(fd,MSG_ERROR);
                continue;
            }

            memcpy(g_memory+dir,(uint8_t*)payload+8,cant);

            pthread_mutex_unlock(&g_mem_mutex);

            free(payload);
            send_signal(fd,MSG_OK);
        }
        else {
            if (size>0) free(payload);
            send_signal(fd,MSG_ERROR);
        }
    }

    close(fd);
    return NULL;
}

static void *cpu_accept_loop(void *arg) {
    (void)arg;

    pthread_detach(pthread_self());

    LOG_INFO("Escuchando CPUs en puerto %d", g_config.cpu_listen_port);

    while (1) {
        int cpu_fd = accept_client(g_server_fd);
        if (cpu_fd < 0) {
            if (errno == EBADF) break;
            continue;
        }

        t_cpu_arg *carg = malloc(sizeof(t_cpu_arg));
        carg->fd = cpu_fd;

        pthread_t tid;
        if (pthread_create(&tid,NULL,cpu_thread,carg)!=0) {
            close(cpu_fd);
            free(carg);
        }
    }

    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc < 3) return EXIT_FAILURE;

    config_defaults();
    config_load(argv[1]);

    g_mem_size = (uint32_t)atoi(argv[2]);
    if (!g_mem_size) return EXIT_FAILURE;

    g_memory = malloc(g_mem_size);
    if (!g_memory) return EXIT_FAILURE;

    memset(g_memory,0,g_mem_size);

    signal(SIGINT,sighandler);
    signal(SIGTERM,sighandler);
    signal(SIGPIPE,SIG_IGN);

    printf("[INFO] Iniciando\n");
    printf("[INFO] Tamaño: %u\n", g_mem_size);

    g_server_fd = create_server(g_config.cpu_listen_port);
    if (g_server_fd < 0) return EXIT_FAILURE;

    pthread_t tid;
    pthread_create(&tid,NULL,cpu_accept_loop,NULL);

    int intentos = 10;
    while (intentos-- > 0) {
        g_km_fd = connect_to(g_config.km_ip,g_config.km_port);
        if (g_km_fd >= 0) break;
        sleep(1);
    }

    if (g_km_fd < 0) return EXIT_FAILURE;

    if (handshake_with_km() < 0) {
        close(g_km_fd);
        return EXIT_FAILURE;
    }

    LOG_INFO("Conectado a Kernel Memory");

    uint8_t tipo;
    uint32_t size;

    while (1) {
        void *payload = recv_msg(g_km_fd,&tipo,&size);
        if (!payload) break;

        if (size>0) free(payload);

        send_signal(g_km_fd,MSG_OK);
    }

    close(g_km_fd);
    close(g_server_fd);
    free(g_memory);

    return EXIT_SUCCESS;
}
