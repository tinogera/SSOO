#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "../../utils/src/utils/protocol.h"
#include "../../utils/src/utils/net_utils.h"

#define DEFAULT_LOG_LEVEL "INFO"
#define DEFAULT_KM_IP "127.0.0.1"
#define DEFAULT_KM_PORT 8002
#define DEFAULT_SWAP_PATH "/tmp/tp_so_swapfile.bin"
#define DEFAULT_SWAP_SIZE 2048
#define DEFAULT_BLOCK_SIZE 64

typedef struct {
    char log_level[16];
    char km_ip[64];
    int km_port;
    char swap_file_path[256];
    uint32_t swap_file_size;
    uint32_t block_size;
} t_swap_config;

static t_swap_config g_config;
static int g_km_fd = -1;

#define LOG_INFO(fmt, ...) \
    do { printf("[INFO] " fmt "\n", ##__VA_ARGS__); fflush(stdout); } while(0)

#define LOG_DEBUG(fmt, ...) \
    do { if (!strcmp(g_config.log_level,"DEBUG")) { \
        printf("[DEBUG] " fmt "\n", ##__VA_ARGS__); fflush(stdout); } } while(0)

#define LOG_ERROR(fmt, ...) \
    do { fprintf(stderr,"[ERROR] " fmt "\n", ##__VA_ARGS__); fflush(stderr); } while(0)

static void config_defaults(void) {
    strncpy(g_config.log_level, DEFAULT_LOG_LEVEL, sizeof(g_config.log_level));
    strncpy(g_config.km_ip, DEFAULT_KM_IP, sizeof(g_config.km_ip));
    g_config.km_port = DEFAULT_KM_PORT;
    strncpy(g_config.swap_file_path, DEFAULT_SWAP_PATH, sizeof(g_config.swap_file_path));
    g_config.swap_file_size = DEFAULT_SWAP_SIZE;
    g_config.block_size = DEFAULT_BLOCK_SIZE;
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
        else if (!strcmp(key,"SWAP_FILE_PATH")) strncpy(g_config.swap_file_path,v,sizeof(g_config.swap_file_path));
        else if (!strcmp(key,"SWAP_FILE_SIZE")) g_config.swap_file_size = (uint32_t)atoi(v);
        else if (!strcmp(key,"BLOCK_SIZE")) g_config.block_size = (uint32_t)atoi(v);
    }

    fclose(f);
}

static void sighandler(int sig) {
    (void)sig;
    printf("\n[INFO] Cerrando...\n");

    if (g_km_fd >= 0) close(g_km_fd);
    exit(0);
}

static int handshake_with_km(void) {
    uint8_t ctype = CLIENT_SWAP;

    if (send(g_km_fd,&ctype,sizeof(ctype),MSG_NOSIGNAL) < 0)
        return -1;

    uint8_t payload[8];
    memcpy(payload,&g_config.block_size,4);
    memcpy(payload+4,&g_config.swap_file_size,4);

    if (send_msg(g_km_fd,MSG_HANDSHAKE_SWAP,payload,sizeof(payload)) < 0)
        return -1;

    uint8_t tipo;
    uint32_t size;

    void *resp = recv_msg(g_km_fd,&tipo,&size);

    if (!resp || tipo != MSG_HANDSHAKE_ACK) {
        if (resp && size>0) free(resp);
        return -1;
    }

    return 0;
}

static void service_loop(void) {
    uint8_t tipo;
    uint32_t size;

    while (1) {
        void *payload = recv_msg(g_km_fd,&tipo,&size);
        if (!payload) break;

        if (tipo == MSG_SWAP_WRITE) {
            uint32_t bloque;
            memcpy(&bloque,payload,sizeof(uint32_t));

            LOG_INFO("Escritura bloque %u", bloque);

            if (size>0) free(payload);
            send_signal(g_km_fd,MSG_OK);
        }
        else if (tipo == MSG_SWAP_READ) {
            uint32_t bloque;
            memcpy(&bloque,payload,sizeof(uint32_t));

            LOG_INFO("Lectura bloque %u", bloque);

            if (size>0) free(payload);

            uint8_t *buf = calloc(1,g_config.block_size);
            send_msg(g_km_fd,MSG_OK,buf,g_config.block_size);
            free(buf);
        }
        else {
            if (size>0) free(payload);
            send_signal(g_km_fd,MSG_ERROR);
        }
    }
}

int main(int argc, char *argv[]) {
    if (argc < 2) return EXIT_FAILURE;

    config_defaults();
    config_load(argv[1]);

    signal(SIGINT,sighandler);
    signal(SIGTERM,sighandler);
    signal(SIGPIPE,SIG_IGN);

    printf("[INFO] Iniciando SWAP\n");
    printf("[INFO] Archivo: %s\n", g_config.swap_file_path);
    printf("[INFO] Tamaño: %u\n", g_config.swap_file_size);
    printf("[INFO] Bloque: %u\n", g_config.block_size);

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

    service_loop();

    close(g_km_fd);
    return EXIT_SUCCESS;
}
