#include "ks_mutex.h"

#include <stdlib.h>
#include <string.h>
#include <utils/sockets.h>
#include <utils/protocolo.h>

// ---------------------------------------------------------------------------
// Estado global
// ---------------------------------------------------------------------------
static t_list*         lista_mutexes;
static pthread_mutex_t lock_lista = PTHREAD_MUTEX_INITIALIZER;

void mutexes_init(void) {
    lista_mutexes = list_create();
}

// ---------------------------------------------------------------------------
// Buscar mutex por nombre — debe llamarse con lock_lista tomado
// ---------------------------------------------------------------------------
static t_ks_mutex* buscar_mutex(const char* nombre) {
    for (int i = 0; i < list_size(lista_mutexes); i++) {
        t_ks_mutex* m = list_get(lista_mutexes, i);
        if (strcmp(m->nombre, nombre) == 0) return m;
    }
    return NULL;
}

// ---------------------------------------------------------------------------
// mutex_ks_create
// ---------------------------------------------------------------------------
int mutex_ks_create(const char* nombre) {
    pthread_mutex_lock(&lock_lista);

    if (buscar_mutex(nombre) != NULL) {
        pthread_mutex_unlock(&lock_lista);
        return -1;
    }

    t_ks_mutex* m = malloc(sizeof(t_ks_mutex));
    m->nombre     = strdup(nombre);
    m->owner_pid  = -1;
    m->cola_espera = queue_create();
    pthread_mutex_init(&m->lock, NULL);

    list_add(lista_mutexes, m);

    pthread_mutex_unlock(&lock_lista);
    return 0;
}

// ---------------------------------------------------------------------------
// mutex_ks_lock — caso libre
// ---------------------------------------------------------------------------
int mutex_ks_lock(uint32_t pid, int fd_cpu, const char* nombre, t_log* logger) {
    pthread_mutex_lock(&lock_lista);
    t_ks_mutex* m = buscar_mutex(nombre);
    if (m == NULL) {
        pthread_mutex_unlock(&lock_lista);
        return -1;
    }
    pthread_mutex_lock(&m->lock);
    pthread_mutex_unlock(&lock_lista);

    if (m->owner_pid == -1) {
        m->owner_pid = (int)pid;
        pthread_mutex_unlock(&m->lock);

        log_info(logger, "## (%u) Toma el Mutex %s", pid, nombre);
        enviar_mensaje(fd_cpu, MSG_OK, NULL, 0);
        return 0;
    }

    // Mutex tomado: encolar al waiter. La CPU queda bloqueada esperando
    // la respuesta; no enviamos MSG_OK hasta que mutex_ks_unlock lo libere.
    t_mutex_waiter* waiter = malloc(sizeof(t_mutex_waiter));
    waiter->pid    = pid;
    waiter->fd_cpu = fd_cpu;
    queue_push(m->cola_espera, waiter);
    pthread_mutex_unlock(&m->lock);
    return 1;
}

// ---------------------------------------------------------------------------
// mutex_ks_unlock
// ---------------------------------------------------------------------------
int mutex_ks_unlock(uint32_t pid, const char* nombre, t_log* logger) {
    pthread_mutex_lock(&lock_lista);
    t_ks_mutex* m = buscar_mutex(nombre);
    if (m == NULL) {
        pthread_mutex_unlock(&lock_lista);
        return -1;
    }
    pthread_mutex_lock(&m->lock);
    pthread_mutex_unlock(&lock_lista);

    if (m->owner_pid != (int)pid) {
        pthread_mutex_unlock(&m->lock);
        return -1;
    }

    log_info(logger, "## (%u) Libera el Mutex %s", pid, nombre);

    t_mutex_waiter* siguiente = queue_pop(m->cola_espera);
    if (siguiente != NULL) {
        m->owner_pid = (int)siguiente->pid;
        pthread_mutex_unlock(&m->lock);

        log_info(logger, "## (%u) Toma el Mutex %s", siguiente->pid, nombre);
        enviar_mensaje(siguiente->fd_cpu, MSG_OK, NULL, 0);
        free(siguiente);
    } else {
        m->owner_pid = -1;
        pthread_mutex_unlock(&m->lock);
    }

    return 0;
}
