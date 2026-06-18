#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <semaphore.h>
#include <time.h>
#include <commons/log.h>
#include <commons/collections/queue.h>
#include <commons/collections/list.h>
#include <commons/config.h>
#include <utils/sockets.h>
#include <utils/protocolo.h>
#include <proceso.h>
#include "ks_mutex.h"

t_log* logger;
static int             fd_km    = -1;
static pthread_mutex_t mutex_km = PTHREAD_MUTEX_INITIALIZER;

t_queue *cola_new, *cola_exec;
t_queue *cola_block, *cola_susp_block, *cola_susp_ready, *cola_exit;

pthread_mutex_t mutex_new, mutex_exec;
pthread_mutex_t mutex_block, mutex_susp_block, mutex_susp_ready, mutex_exit;

#define MAX_COLAS 16
static int             n_colas = 1;
static char            algoritmos_cola[MAX_COLAS][8];
static t_queue*        colas_ready[MAX_COLAS];
static pthread_mutex_t mutex_colas_ready[MAX_COLAS];

static uint32_t        next_pid  = 0;
static pthread_mutex_t mutex_pid = PTHREAD_MUTEX_INITIALIZER;

static char algoritmo[8];
static int  rr_quantum_ms;
static int  suspension_timeout_ms;
static int  queue_preemption = 0;

typedef struct {
    int fd;
    int ocupada;
} t_cpu_entry;

static t_list*         lista_cpus = NULL;
static pthread_mutex_t mutex_cpus = PTHREAD_MUTEX_INITIALIZER;
static sem_t           sem_cpu_disponible;
static sem_t           sem_largo_plazo;

static int             fd_io_sleep  = -1;
static int             fd_io_stdout = -1;
static int             fd_io_stdin  = -1;
static pthread_mutex_t mutex_io     = PTHREAD_MUTEX_INITIALIZER;

t_proceso* crear_proceso(char* path, int prioridad);
void*      atender_cliente(void* arg);
static void atender_cpu(int fd, t_cpu_entry* entry);
static void atender_io(int fd, char* tipo);
static void* thread_planificador(void* _);
static void* thread_quantum_timer(void* arg);
static void* thread_largo_plazo(void* _);
static void* thread_suspension_timer(void* arg);

// Parsea "QUEUES_ALGORITHMS=[FIFO,RR,RR,FIFO]" y carga el array out[][8].
// Retorna la cantidad de colas.
static int parsear_queues_algorithms(const char* str, char out[][8]) {
    char buf[256];
    strncpy(buf, str, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    int n = 0;
    char* p = buf;
    if (*p == '[') p++;
    char* tok = strtok(p, ",]");
    while (tok && n < MAX_COLAS) {
        while (*tok == ' ') tok++;
        strncpy(out[n], tok, 7);
        out[n][7] = '\0';
        n++;
        tok = strtok(NULL, ",]");
    }
    return n > 0 ? n : 1;
}

// Encola el proceso en la cola de su prioridad y señala al planificador.
static void encolar_en_ready(t_proceso* proc) {
    int nivel = proc->prioridad;
    if (nivel < 0 || nivel >= n_colas) nivel = n_colas - 1;
    pthread_mutex_lock(&mutex_colas_ready[nivel]);
    queue_push(colas_ready[nivel], proc);
    pthread_mutex_unlock(&mutex_colas_ready[nivel]);
    sem_post(&sem_cpu_disponible);
}

static const char* estado_str(t_estado e) {
    switch (e) {
        case NEW:        return "NEW";
        case READY:      return "READY";
        case EXEC:       return "EXEC";
        case BLOCK:      return "BLOCK";
        case SUSP_BLOCK: return "SUSP. BLOCK";
        case SUSP_READY: return "SUSP. READY";
        case EXIT:       return "EXIT";
        default:         return "UNKNOWN";
    }
}

static void cambiar_estado(t_proceso* proc, t_estado nuevo) {
    log_info(logger, "## (%d) Pasa del estado %s al estado %s",
             proc->PID, estado_str(proc->estado), estado_str(nuevo));
    proc->estado = nuevo;
}


int main(int argc, char* argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Uso: %s [Archivo Config] [Path Proceso Inicial]\n", argv[0]);
        return EXIT_FAILURE;
    }

    t_config* config = config_create(argv[1]);
    if (!config) { fprintf(stderr, "No se pudo leer config: %s\n", argv[1]); return EXIT_FAILURE; }

    char* log_level_str = config_get_string_value(config, "LOG_LEVEL");
    logger = log_create("kernel_scheduler.log", "KernelScheduler", true, log_level_from_string(log_level_str));
    if (!logger) { fprintf(stderr, "Error al crear logger\n"); config_destroy(config); return EXIT_FAILURE; }

    char* ip_km   = config_get_string_value(config, "KERNEL_MEMORY_IP");
    int puerto_km = config_get_int_value(config, "KERNEL_MEMORY_PORT");
    int puerto_ks = config_get_int_value(config, "KERNEL_SCHEDULER_PORT");
    strncpy(algoritmo, config_get_string_value(config, "PLANIFICATION_ALGORITHM"), sizeof(algoritmo) - 1);
    rr_quantum_ms         = config_get_int_value(config, "RR_QUANTUM");
    suspension_timeout_ms = config_get_int_value(config, "SUSPENSION_TIMEOUT");

    if (strcmp(algoritmo, "CMN") == 0) {
        char* qs = config_get_string_value(config, "QUEUES_ALGORITHMS");
        n_colas = parsear_queues_algorithms(qs, algoritmos_cola);
        char* qp = config_get_string_value(config, "QUEUE_PREEMPTION");
        queue_preemption = (strcmp(qp, "TRUE") == 0) ? 1 : 0;
    } else {
        n_colas = 1;
        strncpy(algoritmos_cola[0], algoritmo, sizeof(algoritmos_cola[0]) - 1);
    }

    // Conectar a KM sincrónicamente antes de crear PID 0
    fd_km = conectar_a_servidor(ip_km, puerto_km);
    if (fd_km == -1) {
        log_error(logger, "No se pudo conectar a Kernel Memory en %s:%d", ip_km, puerto_km);
        config_destroy(config); log_destroy(logger); return EXIT_FAILURE;
    }
    enviar_mensaje(fd_km, MSG_KS_IDENTIFICACION, NULL, 0);
    t_mensaje* resp_km = recibir_mensaje(fd_km);
    if (!resp_km || resp_km->op_code != MSG_OK) {
        log_error(logger, "Kernel Memory rechazo la conexion");
        if (resp_km) free_mensaje(resp_km);
        config_destroy(config); log_destroy(logger); return EXIT_FAILURE;
    }
    free_mensaje(resp_km);
    log_info(logger, "## Conectado a Kernel Memory");

    // Inicializar colas
    cola_new        = queue_create();
    cola_exec       = queue_create();
    cola_block      = queue_create();
    cola_susp_block = queue_create();
    cola_susp_ready = queue_create();
    cola_exit       = queue_create();

    pthread_mutex_init(&mutex_new,        NULL);
    pthread_mutex_init(&mutex_exec,       NULL);
    pthread_mutex_init(&mutex_block,      NULL);
    pthread_mutex_init(&mutex_susp_block, NULL);
    pthread_mutex_init(&mutex_susp_ready, NULL);
    pthread_mutex_init(&mutex_exit,       NULL);

    for (int i = 0; i < n_colas; i++) {
        colas_ready[i] = queue_create();
        pthread_mutex_init(&mutex_colas_ready[i], NULL);
    }

    lista_cpus = list_create();
    sem_init(&sem_cpu_disponible, 0, 0);
    sem_init(&sem_largo_plazo,    0, 0);
    mutexes_init();

    // Servidor
    int fd_servidor = crear_servidor(puerto_ks);
    if (fd_servidor < 0) {
        log_error(logger, "Error al crear servidor en puerto %d", puerto_ks);
        config_destroy(config); log_destroy(logger); return EXIT_FAILURE;
    }

    // Lanzar thread planificador
    pthread_t t_plan;
    pthread_create(&t_plan, NULL, thread_planificador, NULL);
    pthread_detach(t_plan);

    pthread_t t_largo;
    pthread_create(&t_largo, NULL, thread_largo_plazo, NULL);
    pthread_detach(t_largo);

    // PID 0
    if (!crear_proceso(argv[2], 0)) {
        log_error(logger, "No se pudo crear el proceso inicial");
        config_destroy(config); log_destroy(logger); return EXIT_FAILURE;
    }

    // Accept loop
    while (1) {
        int fd_cliente = aceptar_conexion(fd_servidor);
        int* fd_mem = malloc(sizeof(int));
        *fd_mem = fd_cliente;
        pthread_t hilo;
        pthread_create(&hilo, NULL, atender_cliente, fd_mem);
        pthread_detach(hilo);
    }

    config_destroy(config);
    log_destroy(logger);
    return EXIT_SUCCESS;
}


t_proceso* crear_proceso(char* path, int prioridad) {
    pthread_mutex_lock(&mutex_pid);
    uint32_t pid = next_pid++;
    pthread_mutex_unlock(&mutex_pid);

    t_proceso* proc = malloc(sizeof(t_proceso));
    proc->PID                    = (int)pid;
    proc->estado                 = NEW;
    proc->controladorDeProgramas = 0;
    proc->prioridad              = prioridad;
    proc->fd_cpu                 = -1;

    log_info(logger, "## (%u) Se crea el proceso - Estado: NEW", pid);

    uint32_t path_len     = strlen(path) + 1;
    uint32_t payload_size = sizeof(uint32_t) + path_len;
    void*    payload      = malloc(payload_size);
    uint32_t pid_n        = htonl(pid);
    memcpy(payload, &pid_n, sizeof(uint32_t));
    memcpy((char*)payload + sizeof(uint32_t), path, path_len);

    pthread_mutex_lock(&mutex_km);
    enviar_mensaje(fd_km, MSG_CREAR_PROCESO, payload, payload_size);
    free(payload);
    t_mensaje* resp = recibir_mensaje(fd_km);
    pthread_mutex_unlock(&mutex_km);

    if (!resp || resp->op_code != MSG_OK) {
        log_error(logger, "KM rechazo la creacion del proceso %u", pid);
        if (resp) free_mensaje(resp);
        free(proc); return NULL;
    }
    free_mensaje(resp);

    cambiar_estado(proc, READY);
    encolar_en_ready(proc);
    return proc;
}


void* atender_cliente(void* arg) {
    int fd = *((int*)arg);
    free(arg);

    t_mensaje* msg = recibir_mensaje(fd);
    if (!msg) return NULL;

    if (msg->op_code == MSG_CPU_IDENTIFICACION) {
        char* id_cpu = deserializar_string(msg->payload);
        log_info(logger, "## CPU %s Conectada", id_cpu);
        free(id_cpu);
        free_mensaje(msg);
        enviar_mensaje(fd, MSG_OK, NULL, 0);

        t_cpu_entry* entry = malloc(sizeof(t_cpu_entry));
        entry->fd      = fd;
        entry->ocupada = 0;
        pthread_mutex_lock(&mutex_cpus);
        list_add(lista_cpus, entry);
        pthread_mutex_unlock(&mutex_cpus);

        sem_post(&sem_cpu_disponible);
        atender_cpu(fd, entry);

    } else if (msg->op_code == MSG_IO_IDENTIFICACION) {
        char* tipo = deserializar_string(msg->payload);
        log_info(logger, "## IO %s Conectada", tipo);
        free_mensaje(msg);
        enviar_mensaje(fd, MSG_OK, NULL, 0);

        pthread_mutex_lock(&mutex_io);
        if      (strcmp(tipo, "SLEEP")  == 0) fd_io_sleep  = fd;
        else if (strcmp(tipo, "STDOUT") == 0) fd_io_stdout = fd;
        else if (strcmp(tipo, "STDIN")  == 0) fd_io_stdin  = fd;
        pthread_mutex_unlock(&mutex_io);

        atender_io(fd, tipo);
        free(tipo);
    } else {
        log_warning(logger, "Identificacion desconocida: op_code=%u", msg->op_code);
        free_mensaje(msg);
    }

    return NULL;
}


static void despachar(t_proceso* proc, t_cpu_entry* cpu) {
    cambiar_estado(proc, EXEC);
    proc->fd_cpu = cpu->fd;
    cpu->ocupada  = 1;

    pthread_mutex_lock(&mutex_exec);
    queue_push(cola_exec, proc);
    pthread_mutex_unlock(&mutex_exec);

    t_payload_despachar_proceso payload = { .pid = htonl(proc->PID) };
    enviar_mensaje(cpu->fd, MSG_DESPACHAR_PROCESO, &payload, sizeof(payload));
}

static void* thread_quantum_timer(void* arg) {
    int pid = *((int*)arg);
    free(arg);

    struct timespec ts = {
        .tv_sec  = rr_quantum_ms / 1000,
        .tv_nsec = (long)(rr_quantum_ms % 1000) * 1000000L
    };
    nanosleep(&ts, NULL);

    // Si el proceso sigue en EXEC, interrumpir su CPU
    pthread_mutex_lock(&mutex_exec);
    int fd_cpu_target = -1;
    for (int i = 0; i < (int)queue_size(cola_exec); i++) {
        t_proceso* p = list_get(cola_exec->elements, i);
        if (p->PID == pid) { fd_cpu_target = p->fd_cpu; break; }
    }
    pthread_mutex_unlock(&mutex_exec);

    if (fd_cpu_target != -1) {
        t_payload_interrupcion_cpu pay = { .pid = htonl(pid), .motivo = htonl(MOTIVO_INTERRUPCION_QUANTUM) };
        enviar_mensaje(fd_cpu_target, MSG_INTERRUPCION_CPU, &pay, sizeof(pay));
    }

    return NULL;
}

static void* thread_planificador(void* _) {
    (void)_;
    while (1) {
        sem_wait(&sem_cpu_disponible);

        // Seleccionar el proceso de la cola no vacía de mayor prioridad (índice 0 = mayor)
        t_proceso* proc = NULL;
        int nivel_elegido = 0;
        for (int i = 0; i < n_colas; i++) {
            pthread_mutex_lock(&mutex_colas_ready[i]);
            if (queue_size(colas_ready[i]) > 0) {
                proc = queue_pop(colas_ready[i]);
                nivel_elegido = i;
            }
            pthread_mutex_unlock(&mutex_colas_ready[i]);
            if (proc) break;
        }

        if (!proc) continue;

        pthread_mutex_lock(&mutex_cpus);
        t_cpu_entry* cpu = NULL;
        for (int i = 0; i < list_size(lista_cpus); i++) {
            t_cpu_entry* e = list_get(lista_cpus, i);
            if (!e->ocupada) { cpu = e; break; }
        }
        pthread_mutex_unlock(&mutex_cpus);

        if (!cpu) {
            // No hay CPU libre: devolver al fondo de su cola sin señal adicional
            int nivel = proc->prioridad < n_colas ? proc->prioridad : n_colas - 1;
            pthread_mutex_lock(&mutex_colas_ready[nivel]);
            queue_push(colas_ready[nivel], proc);
            pthread_mutex_unlock(&mutex_colas_ready[nivel]);
            continue;
        }

        despachar(proc, cpu);

        if (strcmp(algoritmos_cola[nivel_elegido], "RR") == 0) {
            int* pid_heap = malloc(sizeof(int));
            *pid_heap = proc->PID;
            pthread_t t;
            pthread_create(&t, NULL, thread_quantum_timer, pid_heap);
            pthread_detach(t);
        }
    }
    return NULL;
}


static t_proceso* sacar_de_exec(int pid) {
    pthread_mutex_lock(&mutex_exec);
    t_proceso* proc = NULL;
    int sz = queue_size(cola_exec);
    for (int i = 0; i < sz; i++) {
        t_proceso* q = queue_pop(cola_exec);
        if (q->PID == pid) proc = q;
        else queue_push(cola_exec, q);
    }
    pthread_mutex_unlock(&mutex_exec);

    if (proc) {
        pthread_mutex_lock(&mutex_cpus);
        for (int i = 0; i < list_size(lista_cpus); i++) {
            t_cpu_entry* e = list_get(lista_cpus, i);
            if (e->fd == proc->fd_cpu) { e->ocupada = 0; break; }
        }
        pthread_mutex_unlock(&mutex_cpus);
        proc->fd_cpu = -1;
    }
    return proc;
}

// Mueve el proceso a BLOCK y señala al planificador que la CPU quedó libre.
static void mover_a_block(t_proceso* proc) {
    cambiar_estado(proc, BLOCK);
    pthread_mutex_lock(&mutex_block);
    queue_push(cola_block, proc);
    pthread_mutex_unlock(&mutex_block);
    sem_post(&sem_cpu_disponible);

    int* pid_heap = malloc(sizeof(int));
    *pid_heap = proc->PID;
    pthread_t t;
    pthread_create(&t, NULL, thread_suspension_timer, pid_heap);
    pthread_detach(t);
}

// Saca el proceso de cola_block por PID. Retorna el proceso o NULL.
static t_proceso* sacar_de_block(int pid) {
    pthread_mutex_lock(&mutex_block);
    t_proceso* proc = NULL;
    int sz = queue_size(cola_block);
    for (int i = 0; i < sz; i++) {
        t_proceso* q = queue_pop(cola_block);
        if (q->PID == pid) proc = q;
        else queue_push(cola_block, q);
    }
    pthread_mutex_unlock(&mutex_block);
    return proc;
}


static t_proceso* sacar_de_susp_block(int pid) {
    pthread_mutex_lock(&mutex_susp_block);
    t_proceso* proc = NULL;
    int sz = queue_size(cola_susp_block);
    for (int i = 0; i < sz; i++) {
        t_proceso* q = queue_pop(cola_susp_block);
        if (q->PID == pid) proc = q;
        else queue_push(cola_susp_block, q);
    }
    pthread_mutex_unlock(&mutex_susp_block);
    return proc;
}


static void* thread_suspension_timer(void* arg) {
    int pid = *((int*)arg);
    free(arg);

    struct timespec ts = {
        .tv_sec  = suspension_timeout_ms / 1000,
        .tv_nsec = (long)(suspension_timeout_ms % 1000) * 1000000L
    };
    nanosleep(&ts, NULL);

    t_proceso* proc = sacar_de_block(pid);
    if (!proc) return NULL;

    log_info(logger, "## (%d) - Timeout de IO: suspendiendo proceso", pid);
    cambiar_estado(proc, SUSP_BLOCK);
    pthread_mutex_lock(&mutex_susp_block);
    queue_push(cola_susp_block, proc);
    pthread_mutex_unlock(&mutex_susp_block);
    return NULL;
}

static void* thread_largo_plazo(void* _) {
    (void)_;
    while (1) {
        sem_wait(&sem_largo_plazo);

        pthread_mutex_lock(&mutex_susp_ready);
        t_proceso* proc = queue_size(cola_susp_ready) > 0 ? queue_pop(cola_susp_ready) : NULL;
        pthread_mutex_unlock(&mutex_susp_ready);

        if (!proc) continue;

        // CK2: KM mockea espacio libre — siempre admitimos de SUSP. READY
        cambiar_estado(proc, READY);
        encolar_en_ready(proc);
    }
    return NULL;
}

static void atender_cpu(int fd, t_cpu_entry* entry) {
    while (1) {
        t_mensaje* msg = recibir_mensaje(fd);
        if (!msg) {
            pthread_mutex_lock(&mutex_cpus);
            entry->ocupada = 0;
            pthread_mutex_unlock(&mutex_cpus);
            log_warning(logger, "## CPU fd=%d desconectada", fd);
            return;
        }

        switch (msg->op_code) {

        case MSG_DEVOLVER_PROCESO: {
            t_payload_devolver_proceso* p = msg->payload;
            int pid    = (int)ntohl(p->pid);
            int motivo = (int)ntohl(p->motivo);

            t_proceso* proc = sacar_de_exec(pid);
            if (!proc) break;

            if (motivo == MOTIVO_DEVOLUCION_EXIT) {
                cambiar_estado(proc, EXIT);
                log_info(logger, "## (%d) finalizó su ejecución con motivo de EXIT", pid);
                pthread_mutex_lock(&mutex_exit);
                queue_push(cola_exit, proc);
                pthread_mutex_unlock(&mutex_exit);
                sem_post(&sem_cpu_disponible);

            } else if (motivo == MOTIVO_DEVOLUCION_INTERRUPCION) {
                log_info(logger, "## (%d) - Desalojado por fin de quantum", pid);
                cambiar_estado(proc, READY);
                encolar_en_ready(proc);

            } else {
                // MOTIVO_DEVOLUCION_SYSCALL con proceso todavía en exec: la syscall
                // fue no bloqueante (MUTEX_CREATE, MUTEX_UNLOCK, MUTEX_LOCK libre).
                cambiar_estado(proc, READY);
                encolar_en_ready(proc);
            }
            break;
        }

        case MSG_SYSCALL_SLEEP: {
            t_payload_io_sleep* p = msg->payload;
            int      pid       = (int)ntohl(p->pid);
            uint32_t tiempo_ms = ntohl(p->tiempo_ms);

            log_info(logger, "## (%d) - Solicitó syscall: SLEEP", pid);
            t_proceso* proc = sacar_de_exec(pid);
            if (proc) mover_a_block(proc);

            pthread_mutex_lock(&mutex_io);
            int fd_io = fd_io_sleep;
            pthread_mutex_unlock(&mutex_io);
            if (fd_io != -1) {
                t_payload_io_sleep fwd = { .pid = htonl(pid), .tiempo_ms = htonl(tiempo_ms) };
                enviar_mensaje(fd_io, MSG_IO_SLEEP, &fwd, sizeof(fwd));
            }
            break;
        }

        case MSG_SYSCALL_STDOUT: {
            t_payload_syscall_io_memoria* p = msg->payload;
            int pid = (int)ntohl(p->pid);
            log_info(logger, "## (%d) - Solicitó syscall: STDOUT", pid);

            t_proceso* proc = sacar_de_exec(pid);
            if (proc) mover_a_block(proc);

            // Check 2: KM mockea → enviamos vacío a IO STDOUT
            pthread_mutex_lock(&mutex_io);
            int fd_io = fd_io_stdout;
            pthread_mutex_unlock(&mutex_io);
            if (fd_io != -1) {
                uint32_t pid_n = htonl(pid);
                uint8_t buf[sizeof(uint32_t) + 1];
                memcpy(buf, &pid_n, sizeof(uint32_t));
                buf[sizeof(uint32_t)] = '\0';
                enviar_mensaje(fd_io, MSG_IO_STDOUT, buf, sizeof(buf));
            }
            break;
        }

        case MSG_SYSCALL_STDIN: {
            t_payload_syscall_io_memoria* p = msg->payload;
            int      pid     = (int)ntohl(p->pid);
            uint32_t n_bytes = ntohl(p->tamanio);
            log_info(logger, "## (%d) - Solicitó syscall: STDIN", pid);

            t_proceso* proc = sacar_de_exec(pid);
            if (proc) mover_a_block(proc);

            pthread_mutex_lock(&mutex_io);
            int fd_io = fd_io_stdin;
            pthread_mutex_unlock(&mutex_io);
            if (fd_io != -1) {
                t_payload_io_stdin fwd = { .pid = htonl(pid), .n_bytes = htonl(n_bytes) };
                enviar_mensaje(fd_io, MSG_IO_STDIN, &fwd, sizeof(fwd));
            }
            break;
        }

        case MSG_MUTEX_CREATE: {
            t_payload_mutex* p = msg->payload;
            int pid = (int)ntohl(p->pid);
            log_info(logger, "## (%d) - Solicitó syscall: MUTEX_CREATE", pid);
            mutex_ks_create(p->nombre);
            // No se envía MSG_OK: CPU ya devolvió el proceso, KS re-despacha
            // por el planificador vía MSG_DEVOLVER_PROCESO.
            break;
        }
        case MSG_MUTEX_LOCK: {
            t_payload_mutex* p = msg->payload;
            int pid = (int)ntohl(p->pid);
            log_info(logger, "## (%d) - Solicitó syscall: MUTEX_LOCK", pid);

            t_proceso* proc = sacar_de_exec(pid);
            if (!proc) break;

            int resultado = mutex_ks_lock(pid, p->nombre, logger);
            if (resultado == 0) {
                // Mutex libre: proceso vuelve a READY
                cambiar_estado(proc, READY);
                encolar_en_ready(proc);
            } else if (resultado == 1) {
                // Mutex tomado: proceso va a BLOCK hasta que se libere
                mover_a_block(proc);
            }
            break;
        }
        case MSG_MUTEX_UNLOCK: {
            t_payload_mutex* p = msg->payload;
            int pid = (int)ntohl(p->pid);
            log_info(logger, "## (%d) - Solicitó syscall: MUTEX_UNLOCK", pid);

            // No se envía MSG_OK: CPU ya devolvió el proceso, KS re-despacha
            // por el planificador vía MSG_DEVOLVER_PROCESO.
            int waiter_pid = mutex_ks_unlock(pid, p->nombre, logger);
            if (waiter_pid >= 0) {
                // Mover al waiter de BLOCK → READY (o SUSP_BLOCK → SUSP_READY)
                t_proceso* waiter = sacar_de_block(waiter_pid);
                if (waiter) {
                    cambiar_estado(waiter, READY);
                    encolar_en_ready(waiter);
                } else {
                    waiter = sacar_de_susp_block(waiter_pid);
                    if (waiter) {
                        cambiar_estado(waiter, SUSP_READY);
                        pthread_mutex_lock(&mutex_susp_ready);
                        queue_push(cola_susp_ready, waiter);
                        pthread_mutex_unlock(&mutex_susp_ready);
                        sem_post(&sem_largo_plazo);
                    }
                }
            }
            break;
        }

        case MSG_SYSCALL_EXIT: {
            t_payload_syscall_exit* p = msg->payload;
            int pid = (int)ntohl(p->pid);

            log_info(logger, "## (%d) - Solicitó syscall: EXIT", pid);

            t_proceso* proc = sacar_de_exec(pid);
            if (proc) {
                cambiar_estado(proc, EXIT);
                log_info(logger, "## (%d) finalizó su ejecución con motivo de EXIT", pid);
                pthread_mutex_lock(&mutex_exit);
                queue_push(cola_exit, proc);
                pthread_mutex_unlock(&mutex_exit);
                sem_post(&sem_cpu_disponible);
            }

            enviar_mensaje(fd, MSG_OK, NULL, 0);
            break;
        }

        case MSG_INIT_PROC: {
            if (msg->payload_size < sizeof(uint32_t)) {
                enviar_mensaje(fd, MSG_ERROR, NULL, 0);
                break;
            }
            // Layout del payload: { uint32_t pid_padre, char path[], uint32_t prioridad }
            // pid_padre se reserva para CK3.
            char* path_hijo = (char*)msg->payload + sizeof(uint32_t);
            size_t path_len = strlen(path_hijo) + 1;
            uint32_t prioridad_n;
            memcpy(&prioridad_n,
                   (char*)msg->payload + sizeof(uint32_t) + path_len,
                   sizeof(uint32_t));
            int prioridad = (int)ntohl(prioridad_n);

            crear_proceso(path_hijo, prioridad);
            enviar_mensaje(fd, MSG_OK, NULL, 0);
            break;
        }

        default:
            log_warning(logger, "## KS: op_code desconocido %u", msg->op_code);
            break;
        }

        free_mensaje(msg);
    }
}


static void atender_io(int fd, char* tipo) {
    while (1) {
        t_mensaje* msg = recibir_mensaje(fd);
        if (!msg) {
            log_warning(logger, "## IO %s desconectada", tipo);
            return;
        }

        if (msg->op_code == MSG_IO_FIN) {
            t_payload_io_fin* p = msg->payload;
            int pid = (int)ntohl(p->pid);

            t_proceso* proc = sacar_de_block(pid);
            if (proc) {
                // IO terminó antes del timeout: BLOCK → READY
                cambiar_estado(proc, READY);
                log_info(logger, "## (%d) finalizó IO y pasa a READY", pid);
                encolar_en_ready(proc);
            } else {
                // IO terminó después del timeout: SUSP. BLOCK → SUSP. READY
                proc = sacar_de_susp_block(pid);
                if (proc) {
                    cambiar_estado(proc, SUSP_READY);
                    log_info(logger, "## (%d) finalizó IO y pasa a SUSP. READY", pid);
                    pthread_mutex_lock(&mutex_susp_ready);
                    queue_push(cola_susp_ready, proc);
                    pthread_mutex_unlock(&mutex_susp_ready);
                    sem_post(&sem_largo_plazo);
                }
            }

        } else if (msg->op_code == MSG_IO_STDIN_DATOS) {
            uint32_t pid_n;
            memcpy(&pid_n, msg->payload, sizeof(uint32_t));
            int pid = (int)ntohl(pid_n);

            t_proceso* proc = sacar_de_block(pid);
            if (proc) {
                // IO terminó antes del timeout: BLOCK → READY (CK2: KM mockea escritura)
                cambiar_estado(proc, READY);
                log_info(logger, "## (%d) finalizó IO y pasa a READY", pid);
                encolar_en_ready(proc);
            } else {
                // IO terminó después del timeout: SUSP. BLOCK → SUSP. READY
                proc = sacar_de_susp_block(pid);
                if (proc) {
                    cambiar_estado(proc, SUSP_READY);
                    log_info(logger, "## (%d) finalizó IO y pasa a SUSP. READY", pid);
                    pthread_mutex_lock(&mutex_susp_ready);
                    queue_push(cola_susp_ready, proc);
                    pthread_mutex_unlock(&mutex_susp_ready);
                    sem_post(&sem_largo_plazo);
                }
            }
        }

        free_mensaje(msg);
    }
}
