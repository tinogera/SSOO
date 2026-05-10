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
