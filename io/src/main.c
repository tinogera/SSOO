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

#define DEFAULT_LOG_LEVEL  "INFO"
#define DEFAULT_KS_IP      "127.0.0.1"
#define DEFAULT_KS_PORT    8001

typedef struct {
    char log_level[16];
    char ks_ip[64];
    int  ks_port;
} t_io_config;

static t_io_config g_config;
static int         g_ks_fd   = -1;
static uint8_t     g_io_type = 0;

#define LOG_INFO(fmt, ...)  printf("[INFO]  " fmt "\n", ##__VA_ARGS__); fflush(stdout)
#define LOG_DEBUG(fmt, ...) \
    do { if (strcmp(g_config.log_level,"DEBUG")==0) { \
         printf("[DEBUG] " fmt "\n", ##__VA_ARGS__); fflush(stdout); } } while(0)
#define LOG_ERROR(fmt, ...) fprintf(stderr,"[ERROR] " fmt "\n", ##__VA_ARGS__); fflush(stderr)

static void config_defaults(void) {
    strncpy(g_config.log_level, DEFAULT_LOG_LEVEL, sizeof(g_config.log_level));
    strncpy(g_config.ks_ip,     DEFAULT_KS_IP,     sizeof(g_config.ks_ip));
    g_config.ks_port = DEFAULT_KS_PORT;
}

static void config_load(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) { fprintf(stderr,"No se pudo abrir config '%s'\n", path); return; }

    char line[512], key[128], value[384];
    while (fgets(line, sizeof(line), f)) {
        if (line[0]=='#'||line[0]=='\n'||line[0]=='\r') continue;
        if (sscanf(line, "%127[^=]=%383[^\n]", key, value) != 2) continue;
        char *v = value; while (*v==' ') v++;

        if      (strcmp(key,"LOG_LEVEL")==0) strncpy(g_config.log_level, v, sizeof(g_config.log_level));
        else if (strcmp(key,"KS_IP")    ==0) strncpy(g_config.ks_ip, v, sizeof(g_config.ks_ip));
        else if (strcmp(key,"KS_PORT")  ==0) g_config.ks_port = atoi(v);
    }
    fclose(f);
}

static void sighandler(int sig) {
    (void)sig;
    printf("\n[INFO]  Cerrando IO...\n");
    if (g_ks_fd >= 0) close(g_ks_fd);
    exit(0);
}

static int handshake_with_ks(void) {
    uint8_t ctype = CLIENT_IO;
    if (send(g_ks_fd, &ctype, sizeof(ctype), MSG_NOSIGNAL) < 0) {
        perror("send CLIENT_IO"); return -1;
    }

    if (send_msg(g_ks_fd, MSG_HANDSHAKE_IO, &g_io_type, sizeof(g_io_type)) < 0) {
        perror("send_msg MSG_HANDSHAKE_IO"); return -1;
    }

    uint8_t tipo; uint32_t size;
    void *resp = recv_msg(g_ks_fd, &tipo, &size);
    if (!resp || tipo != MSG_HANDSHAKE_ACK) {
        LOG_ERROR("Handshake con Kernel Scheduler fallido");
        if (size > 0 && resp) free(resp);
        return -1;
    }
    return 0;
}

static void do_sleep(int fd, uint32_t pid, uint32_t tiempo_ms) {
    LOG_INFO("## PID: %u - Haciendo sleep por %u milisegundos.", pid, tiempo_ms);
    usleep((useconds_t)tiempo_ms * 1000);
    LOG_INFO("## PID: %u - Fin de IO", pid);
    send_signal(fd, MSG_OK);
}

static void do_stdin(int fd, uint32_t pid, uint32_t cant) {
    LOG_INFO("## PID: %u - Inicio de IO", pid);
    LOG_INFO("## PID: %u - Ingrese %u caracteres:", pid, cant);

    char *buf = calloc(1, cant + 2);
    if (!buf) { send_signal(fd, MSG_ERROR); return; }

    if (fgets(buf, (int)(cant + 2), stdin) == NULL) {
        memset(buf, 0, cant);
    }

    buf[cant] = '\0';
    size_t leido = strlen(buf);
    if (leido > 0 && buf[leido-1] == '\n') { buf[leido-1] = '\0'; leido--; }

    LOG_INFO("## PID: %u - Fin de IO", pid);
    send_msg(fd, MSG_OK, buf, cant);
    free(buf);
}

static void do_stdout(int fd, uint32_t pid, const uint8_t *datos, uint32_t cant) {
    LOG_INFO("## PID: %u - Inicio de IO", pid);

    char *str = malloc(cant + 1);
    memcpy(str, datos, cant);
    str[cant] = '\0';

    LOG_INFO("## PID: %u - %s", pid, str);
    free(str);

    LOG_INFO("## PID: %u - Fin de IO", pid);
    send_signal(fd, MSG_OK);
}

static void service_loop(void) {
    uint8_t  tipo;
    uint32_t size;

    while (1) {
        void *payload = recv_msg(g_ks_fd, &tipo, &size);
        if (!payload) {
            LOG_INFO("Kernel Scheduler desconectado. Cerrando IO.");
            break;
        }

        if (size < 8) {
            LOG_ERROR("Payload de IO muy corto (%u bytes)", size);
            if (size > 0) free(payload);
            send_signal(g_ks_fd, MSG_ERROR);
            continue;
        }

        uint32_t pid, param;
        memcpy(&pid,   (uint8_t*)payload,   4);
        memcpy(&param, (uint8_t*)payload+4, 4);

        switch (g_io_type) {
            case 3:
                do_sleep(g_ks_fd, pid, param);
                if (size > 0) free(payload);
                break;
            case 1:
                do_stdin(g_ks_fd, pid, param);
                if (size > 0) free(payload);
                break;
            case 2:
                if (size > 8) {
                    do_stdout(g_ks_fd, pid,
                              (uint8_t*)payload + 8,
                              size - 8);
                } else {
                    do_stdout(g_ks_fd, pid, (uint8_t*)"", 0);
                }
                if (size > 0) free(payload);
                break;
            default:
                LOG_ERROR("Tipo de IO desconocido: %u", g_io_type);
                if (size > 0) free(payload);
                send_signal(g_ks_fd, MSG_ERROR);
                break;
        }
    }
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Uso: %s [Archivo Config] [Tipo: STDIN|STDOUT|SLEEP]\n", argv[0]);
        return EXIT_FAILURE;
    }

    config_defaults();
    config_load(argv[1]);

    const char *tipo_str = argv[2];
    if      (strcmp(tipo_str, "STDIN")  == 0) g_io_type = 1;
    else if (strcmp(tipo_str, "STDOUT") == 0) g_io_type = 2;
    else if (strcmp(tipo_str, "SLEEP")  == 0) g_io_type = 3;
    else {
        fprintf(stderr, "[ERROR] Tipo de IO inválido: '%s'\n", tipo_str);
        return EXIT_FAILURE;
    }

    signal(SIGINT,  sighandler);
    signal(SIGTERM, sighandler);
    signal(SIGPIPE, SIG_IGN);

    printf("[INFO]  === IO [%s] iniciando ===\n", tipo_str);
    printf("[INFO]  Conectando a Kernel Scheduler %s:%d...\n",
           g_config.ks_ip, g_config.ks_port);
    fflush(stdout);

    int intentos = 10;
    while (intentos-- > 0) {
        g_ks_fd = connect_to(g_config.ks_ip, g_config.ks_port);
        if (g_ks_fd >= 0) break;
        printf("[INFO]  Reintentando... (%d)\n", intentos);
        sleep(1);
    }
    if (g_ks_fd < 0) {
        fprintf(stderr, "[ERROR] No se pudo conectar al Kernel Scheduler.\n");
        return EXIT_FAILURE;
    }

    if (handshake_with_ks() < 0) {
        close(g_ks_fd); return EXIT_FAILURE;
    }

    LOG_INFO("## Conectado a Kernel Scheduler");

    service_loop();

    close(g_ks_fd);
    return EXIT_SUCCESS;
}
