#ifndef KS_MUTEX_H_
#define KS_MUTEX_H_

#include <stdint.h>
#include <pthread.h>
#include <commons/collections/list.h>
#include <commons/collections/queue.h>
#include <commons/log.h>

/*
 * Entrada de la cola de espera de un mutex.
 * Solo guardamos el PID; el KS mueve el proceso a BLOCK y lo re-despacha
 * por el planificador normal cuando el mutex se libera.
 */
typedef struct {
    uint32_t pid;
} t_mutex_waiter;

/*
 * Representa un mutex del sistema.
 * owner_pid == -1 indica que el mutex está libre.
 * cola_espera contiene t_mutex_waiter* en orden FIFO.
 */
typedef struct {
    char*           nombre;
    int             owner_pid;
    t_queue*        cola_espera;
    pthread_mutex_t lock;
} t_ks_mutex;

/* Inicializa la lista global de mutexes. Llamar una vez al arrancar el KS. */
void mutexes_init(void);

/*
 * Crea un mutex con el nombre dado si no existe ya.
 * Devuelve 0 en éxito, -1 si ya existía.
 */
int mutex_ks_create(const char* nombre);

/*
 * Intenta tomar el mutex en nombre del proceso pid.
 * - Si está libre: toma el mutex, loguea y devuelve 0.
 * - Si está tomado: encola al waiter (solo PID) y devuelve 1.
 * El llamador es responsable de mover el proceso a READY (libre) o BLOCK (tomado).
 * Devuelve -1 si el mutex no existe.
 */
int mutex_ks_lock(uint32_t pid, const char* nombre, t_log* logger);

/*
 * Libera el mutex en nombre del proceso pid.
 * Si hay waiters, desencola el primero y lo convierte en el nuevo owner.
 * Devuelve el PID del nuevo owner (>= 0) si había waiter, -1 si no había
 * o si el mutex no existe / pid no es el owner.
 * El llamador es responsable de mover ese proceso de BLOCK a READY.
 */
int mutex_ks_unlock(uint32_t pid, const char* nombre, t_log* logger);

#endif
