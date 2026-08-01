#include "ks_mutex.h"

#include <limits.h>
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
    m->nombre                   = strdup(nombre);
    m->owner_pid                = -1;
    m->owner_prioridad_original = -1;
    m->cola_espera              = queue_create();
    pthread_mutex_init(&m->lock, NULL);

    list_add(lista_mutexes, m);

    pthread_mutex_unlock(&lock_lista);
    return 0;
}

// ---------------------------------------------------------------------------
// mutex_ks_lock
// ---------------------------------------------------------------------------
int mutex_ks_lock(uint32_t pid, int prioridad, const char* nombre, t_log* logger,
                  int* owner_a_elevar, int* nueva_prioridad_owner) {
    *owner_a_elevar       = -1;
    *nueva_prioridad_owner = -1;

    pthread_mutex_lock(&lock_lista);
    t_ks_mutex* m = buscar_mutex(nombre);
    if (m == NULL) {
        pthread_mutex_unlock(&lock_lista);
        return -1;
    }
    pthread_mutex_lock(&m->lock);
    pthread_mutex_unlock(&lock_lista);

    if (m->owner_pid == -1) {
        m->owner_pid                = (int)pid;
        m->owner_prioridad_original = prioridad;
        pthread_mutex_unlock(&m->lock);
        log_info(logger, "## (%u) Toma el Mutex %s", pid, nombre);
        return 0;
    }

    // Mutex tomado: encolar al waiter con su prioridad para herencia posterior.
    t_mutex_waiter* waiter = malloc(sizeof(t_mutex_waiter));
    waiter->pid      = pid;
    waiter->prioridad = prioridad;
    queue_push(m->cola_espera, waiter);
    log_debug(logger, "Mutex %s: (%u) queda esperando — %d proceso(s) en cola de espera",
              nombre, pid, (int)queue_size(m->cola_espera));

    // Herencia de prioridades: se señala siempre al owner. Quién decide si hay
    // que moverle la prioridad es el llamador, recalculando la efectiva contra
    // todos los mutex que posee — no contra este solo.
    *owner_a_elevar        = m->owner_pid;
    *nueva_prioridad_owner = prioridad;

    pthread_mutex_unlock(&m->lock);
    return 1;
}

// ---------------------------------------------------------------------------
// mutex_ks_unlock
// ---------------------------------------------------------------------------
int mutex_ks_unlock(uint32_t pid, const char* nombre, t_log* logger,
                    int* prioridad_restaurar) {
    *prioridad_restaurar = -1;

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
    *prioridad_restaurar = m->owner_prioridad_original;
    log_debug(logger, "Mutex %s: liberado por (%u) — %d proceso(s) esperando",
              nombre, pid, (int)queue_size(m->cola_espera));

    if (queue_size(m->cola_espera) > 0) {
        t_mutex_waiter* siguiente = queue_pop(m->cola_espera);
        uint32_t waiter_pid       = siguiente->pid;
        m->owner_pid                = (int)waiter_pid;
        m->owner_prioridad_original = siguiente->prioridad;
        free(siguiente);
        pthread_mutex_unlock(&m->lock);
        log_info(logger, "## (%u) Toma el Mutex %s", waiter_pid, nombre);
        return (int)waiter_pid;
    }

    m->owner_pid                = -1;
    m->owner_prioridad_original = -1;
    pthread_mutex_unlock(&m->lock);
    return -1;
}

// ---------------------------------------------------------------------------
// mutex_ks_prioridad_heredada
// ---------------------------------------------------------------------------
int mutex_ks_prioridad_heredada(uint32_t pid) {
    int mejor = INT_MAX;

    // Mismo orden de toma que el resto del archivo (lista y después mutex),
    // así que no puede haber deadlock con lock/unlock corriendo en paralelo.
    pthread_mutex_lock(&lock_lista);
    for (int i = 0; i < list_size(lista_mutexes); i++) {
        t_ks_mutex* m = list_get(lista_mutexes, i);
        pthread_mutex_lock(&m->lock);
        if (m->owner_pid == (int)pid) {
            t_list* esperando = m->cola_espera->elements;
            for (int j = 0; j < list_size(esperando); j++) {
                t_mutex_waiter* w = list_get(esperando, j);
                if (w->prioridad < mejor) mejor = w->prioridad;
            }
        }
        pthread_mutex_unlock(&m->lock);
    }
    pthread_mutex_unlock(&lock_lista);

    return mejor;
}
