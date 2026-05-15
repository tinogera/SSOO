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

typedef enum { IO_STDIN=1, IO_STDOUT=2, IO_SLEEP=3 } t_io_type;

#define DEFAULT_LOG_LEVEL "INFO"
#define DEFAULT_KM_IP "127.0.0.1"
#define DEFAULT_KM_PORT 8002
#define DEFAULT_CPU_IO_PORT 8001
#define DEFAULT_PLANIF_ALG "FIFO"
#define DEFAULT_RR_QUANTUM 1500
#define DEFAULT_SUSPENSION_TO 35000

typedef struct {
    char log_level[16];
    char km_ip[64];
    int km_port;
    int listen_port;
    char planification_algorithm[8];
    int rr_quantum;
    int suspension_timeout;
} t_ks_config;

static t_ks_config g_config;
static int g_km_fd = -1;
static int g_server_fd = -1;
static pthread_mutex_t g_log_mutex = PTHREAD_MUTEX_INITIALIZER;

static uint32_t g_next_pid = 0;
static pthread_mutex_t g_pid_mutex = PTHREAD_MUTEX_INITIALIZER;

static uint32_t next_pid(void) {
    pthread_mutex_lock(&g_pid_mutex);
    uint32_t pid = g_next_pid++;
    pthread_mutex_unlock(&g_pid_mutex);
    return pid;
}

#define LOG_INFO(fmt, ...) \
    do { pthread_mutex_lock(&g_log_mutex); \
         printf("[INFO] " fmt "\n", ##__VA_ARGS__); fflush(stdout); \
         pthread_mutex_unlock(&g_log_mutex); } while(0)

#define LOG_DEBUG(fmt, ...) \
    do { if (strcmp(g_config.log_level,"DEBUG")==0) { \
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
    g_config.listen_port = DEFAULT_CPU_IO_PORT;
    strncpy(g_config.planification_algorithm, DEFAULT_PLANIF_ALG, sizeof(g_config.planification_algorithm));
    g_config.rr_quantum = DEFAULT_RR_QUANTUM;
    g_config.suspension_timeout = DEFAULT_SUSPENSION_TO;
}

static void config_load(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr,"No se pudo abrir config '%s'\n", path);
        return;
    }

    char line[512], key[128], value[384];
    while (fgets(line, sizeof(line), f)) {
        if (line[0]=='#'||line[0]=='\n'||line[0]=='\r') continue;
        if (sscanf(line, "%127[^=]=%383[^\n]", key, value) != 2) continue;

        char *v = value;
        while (*v==' ') v++;

        if (!strcmp(key,"LOG_LEVEL")) strncpy(g_config.log_level, v, sizeof(g_config.log_level));
        else if (!strcmp(key,"KM_IP")) strncpy(g_config.km_ip, v, sizeof(g_config.km_ip));
        else if (!strcmp(key,"KM_PORT")) g_config.km_port = atoi(v);
        else if (!strcmp(key,"LISTEN_PORT")) g_config.listen_port = atoi(v);
        else if (!strcmp(key,"PLANIFICATION_ALGORITHM")) strncpy(g_config.planification_algorithm, v, sizeof(g_config.planification_algorithm));
        else if (!strcmp(key,"RR_QUANTUM")) g_config.rr_quantum = atoi(v);
        else if (!strcmp(key,"SUSPENSION_TIMEOUT")) g_config.suspension_timeout = atoi(v);
    }

    fclose(f);
}

static int handshake_with_km(void) {
    uint8_t ctype = CLIENT_KERNEL_SCHEDULER;

    if (send(g_km_fd, &ctype, sizeof(ctype), MSG_NOSIGNAL) < 0) {
        perror("send");
        return -1;
    }

    if (send_signal(g_km_fd, MSG_HANDSHAKE_KS) < 0) {
        perror("send_signal");
        return -1;
    }

    uint8_t tipo;
    uint32_t size;

    void *resp = recv_msg(g_km_fd, &tipo, &size);
    if (!resp || tipo != MSG_HANDSHAKE_ACK) {
        if (resp && size > 0) free(resp);
        return -1;
    }

    return 0;
}

static int km_create_process(uint32_t pid, const char *path) {
    uint16_t plen = (uint16_t)strlen(path);
    uint32_t payload_size = sizeof(uint32_t) + sizeof(uint16_t) + plen;

    uint8_t *payload = malloc(payload_size);
    memcpy(payload, &pid, 4);
    memcpy(payload + 4, &plen, 2);
    memcpy(payload + 6, path, plen);

    int ret = send_msg(g_km_fd, MSG_KS_CREATE_PROC, payload, payload_size);
    free(payload);
    if (ret < 0) return -1;

    uint8_t tipo;
    uint32_t size;

    void *resp = recv_msg(g_km_fd, &tipo, &size);
    if (resp && size > 0) free(resp);

    return (resp && tipo == MSG_OK) ? 0 : -1;
}

typedef struct { int fd; } t_client_arg;

static void *handle_cpu_thread(void *arg) {
    t_client_arg *carg = arg;
    int fd = carg->fd;
    free(carg);

    pthread_detach(pthread_self());

    uint8_t tipo;
    uint32_t size;

    void *payload = recv_msg(fd, &tipo, &size);
    if (!payload || tipo != MSG_HANDSHAKE_CPU || size < sizeof(uint32_t)) {
        if (payload && size > 0) free(payload);
        close(fd);
        return NULL;
    }

    uint32_t cpu_id;
    memcpy(&cpu_id, payload, sizeof(uint32_t));
    free(payload);

    LOG_INFO("CPU %u conectada", cpu_id);

    send_signal(fd, MSG_HANDSHAKE_ACK);

    while (1) {
        payload = recv_msg(fd, &tipo, &size);
        if (!payload) {
            LOG_INFO("CPU %u desconectada", cpu_id);
            break;
        }

        if (size > 0) free(payload);
        LOG_DEBUG("CPU %u tipo=0x%02X", cpu_id, tipo);
    }

    close(fd);
    return NULL;
}

static void *handle_io_thread(void *arg) {
    t_client_arg *carg = arg;
    int fd = carg->fd;
    free(carg);

    pthread_detach(pthread_self());

    uint8_t tipo;
    uint32_t size;

    void *payload = recv_msg(fd, &tipo, &size);
    if (!payload || tipo != MSG_HANDSHAKE_IO || size < sizeof(uint8_t)) {
        if (payload && size > 0) free(payload);
        close(fd);
        return NULL;
    }

    uint8_t io_type;
    memcpy(&io_type, payload, sizeof(uint8_t));
    free(payload);

    const char *io_name =
        (io_type==IO_STDIN) ? "STDIN" :
        (io_type==IO_STDOUT) ? "STDOUT" :
        (io_type==IO_SLEEP) ? "SLEEP" : "UNKNOWN";

    LOG_INFO("IO %s conectada (fd=%d)", io_name, fd);

    send_signal(fd, MSG_HANDSHAKE_ACK);

    while (1) {
        payload = recv_msg(fd, &tipo, &size);
        if (!payload) {
            LOG_INFO("IO %s desconectada", io_name);
            break;
        }

        if (size > 0) free(payload);
        LOG_DEBUG("IO %s tipo=0x%02X", io_name, tipo);
    }

    close(fd);
    return NULL;
}

static void *client_dispatcher(void *arg) {
    t_client_arg *carg = arg;
    int fd = carg->fd;
    free(carg);

    pthread_detach(pthread_self());

    uint8_t ctype;
    if (recv(fd, &ctype, sizeof(ctype), MSG_WAITALL) <= 0) {
        close(fd);
        return NULL;
    }

    t_client_arg *new_arg = malloc(sizeof(t_client_arg));
    new_arg->fd = fd;

    pthread_t tid;

    switch ((t_client_type)ctype) {
        case CLIENT_CPU:
            pthread_create(&tid, NULL, handle_cpu_thread, new_arg);
            break;
        case CLIENT_IO:
            pthread_create(&tid, NULL, handle_io_thread, new_arg);
            break;
        default:
            free(new_arg);
            close(fd);
            break;
    }
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <commons/log.h>
#include <commons/config.h>
#include <utils/sockets.h>
#include <utils/protocolo.h>
#include "ks_mutex.h"

t_log* logger;

void* atender_cliente(void* arg);
void* handshake_kernel_memory(void* arg);

int main(int argc, char* argv[]) {
    if (argc < 4)
    {
        fprintf(stderr, "Cantidad de argumentos invalida.\n");
        fprintf(stderr, "Uso: %s [IP_KM] [PUERTO_KM] [PUERTO_ESCUCHA]\n", argv[0]);
        return EXIT_FAILURE;
    }

    char* ip_km          = argv[1];
    char* puerto_km      = argv[2];
    char* puerto_escucha = argv[3];

    int puerto_km_int      = atoi(puerto_km);
    int puerto_escucha_int = atoi(puerto_escucha);

    logger = log_create("kernel_scheduler.log", "KernelScheduler", true, LOG_LEVEL_INFO);
    if (logger == NULL) {
        fprintf(stderr, "Error al crear el logger\n");
        return EXIT_FAILURE;
    }

    // conectar a Kernel Memory
    int fd_km = conectar_a_servidor(ip_km, puerto_km_int);
    if (fd_km == -1) {
        log_error(logger, "No se pudo conectar a Kernel Memory");
        return EXIT_FAILURE;
    }

    // crear thread para handshake con KM (NO bloquea main)
    pthread_t thread_km;

    int* fd_km_mem = malloc(sizeof(int));
    *fd_km_mem = fd_km;

    pthread_create(&thread_km, NULL, handshake_kernel_memory, fd_km_mem);
    pthread_detach(thread_km);

    // crear servidor para CPUs / IO
    int fd_servidor = crear_servidor(puerto_escucha_int);
    
    if (fd_servidor < 0) {
        log_error(logger, "Error al crear servidor");
        return EXIT_FAILURE;
    }

    mutexes_init();

    // aceptar conexiones concurrentes
    while(1){
        int fd_cliente = aceptar_conexion(fd_servidor);
        pthread_t hilo;

        int* fd_cliente_mem = malloc(sizeof(int));
        *fd_cliente_mem = fd_cliente;

        pthread_create(&hilo, NULL, atender_cliente, fd_cliente_mem);
        pthread_detach(hilo);
    }

    return 0;
}

// ---------------- THREAD HANDSHAKE KM ----------------

void* handshake_kernel_memory(void* arg) {
    int fd_km = *((int*)arg);
    free(arg);

    // mandar handshake
    enviar_mensaje(fd_km, MSG_KS_IDENTIFICACION, NULL, 0);

    // esperar respuesta
    t_mensaje* msg = recibir_mensaje(fd_km);

    if (msg == NULL) {
        log_error(logger, "No se recibio respuesta de Kernel Memory");
        return NULL;
    }

    if (msg->op_code != MSG_OK) {
        log_error(logger, "Kernel Memory rechazo la conexion");
        free_mensaje(msg);
        return NULL;
    }

    free_mensaje(msg);

    log_info(logger, "## Conectado a Kernel Memory");

    return NULL;
}

static void *accept_loop(void *arg) {
    (void)arg;

    pthread_detach(pthread_self());

    LOG_INFO("Escuchando en puerto %d", g_config.listen_port);

    while (1) {
        int client_fd = accept_client(g_server_fd);
        if (client_fd < 0) {
            if (errno == EBADF) break;
            continue;
        }

        t_client_arg *arg2 = malloc(sizeof(t_client_arg));
        arg2->fd = client_fd;

        pthread_t tid;
        if (pthread_create(&tid, NULL, client_dispatcher, arg2) != 0) {
            close(client_fd);
            free(arg2);
        }
    }

    return NULL;
}

static void sighandler(int sig) {
    (void)sig;
    printf("\n[INFO] Cerrando...\n");

    if (g_km_fd >= 0) close(g_km_fd);
    if (g_server_fd >= 0) close(g_server_fd);

    exit(0);
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Uso: %s [config] [proceso]\n", argv[0]);
        return EXIT_FAILURE;
    }

    config_defaults();
    config_load(argv[1]);

    const char *proceso_inicial = argv[2];

    signal(SIGINT, sighandler);
    signal(SIGTERM, sighandler);
    signal(SIGPIPE, SIG_IGN);

    printf("[INFO] Iniciando\n");
    printf("[INFO] Algoritmo: %s\n", g_config.planification_algorithm);
    printf("[INFO] Proceso inicial: %s\n", proceso_inicial);

    g_server_fd = create_server(g_config.listen_port);
    if (g_server_fd < 0) {
        fprintf(stderr, "Error creando servidor\n");
        return EXIT_FAILURE;
    }

    pthread_t accept_tid;
    pthread_create(&accept_tid, NULL, accept_loop, NULL);

    int intentos = 10;
    while (intentos-- > 0) {
        g_km_fd = connect_to(g_config.km_ip, g_config.km_port);
        if (g_km_fd >= 0) break;
        sleep(1);
    }

    if (g_km_fd < 0) return EXIT_FAILURE;

    if (handshake_with_km() < 0) {
        close(g_km_fd);
        return EXIT_FAILURE;
    }

    LOG_INFO("Conectado a Kernel Memory");

    uint32_t pid0 = next_pid();
    LOG_INFO("Proceso %u creado (NEW)", pid0);

    if (km_create_process(pid0, proceso_inicial) < 0) {
        close(g_km_fd);
        return EXIT_FAILURE;
    }

    LOG_INFO("Proceso %u READY", pid0);

    uint8_t tipo;
    uint32_t size;

    while (1) {
        void *payload = recv_msg(g_km_fd, &tipo, &size);
        if (!payload) break;

        if (size > 0) free(payload);

        if (tipo == MSG_KM_CORRUPTION) {
            LOG_INFO("Memoria corrupta, cerrando");
            break;
        }
    }

    close(g_km_fd);
    close(g_server_fd);

    return EXIT_SUCCESS;
}
void* atender_cliente(void* arg){
    int fd_cliente = *((int*)arg);
    free(arg);

    // --- identificación inicial ---
    t_mensaje* msg = recibir_mensaje(fd_cliente);
    if(msg == NULL) return NULL;

    int es_cpu = 0;

    if(msg->op_code == MSG_CPU_IDENTIFICACION){
        char* id_cpu = deserializar_string(msg->payload);
        log_info(logger, "## CPU %s Conectada", id_cpu);
        free(id_cpu);
        enviar_mensaje(fd_cliente, MSG_OK, NULL, 0);
        es_cpu = 1;
    }
    else if(msg->op_code == MSG_IO_IDENTIFICACION){
        char* tipo = deserializar_string(msg->payload);
        log_info(logger, "## IO %s Conectada", tipo);
        free(tipo);
        enviar_mensaje(fd_cliente, MSG_OK, NULL, 0);
    }
    else {
        log_warning(logger, "Identificación desconocida: op_code=%u", msg->op_code);
    }

    free_mensaje(msg);

    // --- loop de atención (solo CPUs por ahora) ---
    if (!es_cpu) return NULL;

    t_mensaje* pedido;
    while((pedido = recibir_mensaje(fd_cliente)) != NULL) {

        if (pedido->op_code == MSG_MUTEX_CREATE) {
            t_payload_mutex* p = pedido->payload;
            mutex_ks_create(p->nombre);
            enviar_mensaje(fd_cliente, MSG_OK, NULL, 0);
        }
        else if (pedido->op_code == MSG_MUTEX_LOCK) {
            t_payload_mutex* p = pedido->payload;
            uint32_t pid = p->pid;
            // mutex_ks_lock responde MSG_OK directamente si el mutex está libre,
            // o no responde hasta que mutex_ks_unlock desbloquee al waiter.
            mutex_ks_lock(pid, fd_cliente, p->nombre, logger);
        }
        else if (pedido->op_code == MSG_MUTEX_UNLOCK) {
            t_payload_mutex* p = pedido->payload;
            mutex_ks_unlock(p->pid, p->nombre, logger);
            enviar_mensaje(fd_cliente, MSG_OK, NULL, 0);
        }
        else {
            log_warning(logger, "op_code no manejado en KS: %u", pedido->op_code);
            enviar_mensaje(fd_cliente, MSG_ERROR, NULL, 0);
        }

        free_mensaje(pedido);
    }

    return NULL;
}
