#ifndef KS_MUTEX_H_
#define KS_MUTEX_H_

#include <stdint.h>
#include <pthread.h>
#include <commons/collections/list.h>
#include <commons/collections/queue.h>
#include <commons/log.h>

/*
 * Entrada de la cola de espera de un mutex.
 * Guardamos el fd del socket de la CPU que está bloqueada para poder
 * enviarle MSG_OK cuando el mutex se libere.
 */
typedef struct {
    uint32_t pid;
    int      fd_cpu;
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
 * Intenta tomar el mutex en nombre del proceso pid / fd_cpu.
 * - Si está libre: lo toma, loguea y responde MSG_OK al fd_cpu.
 * - Si está tomado: encola al waiter; NO responde (la CPU queda bloqueada
 *   esperando la respuesta, lo que bloquea el hilo del KS que atiende esa CPU).
 * Devuelve 1 si quedó bloqueado (la CPU espera), 0 si tomó sin bloqueo, -1 si error.
 */
int mutex_ks_lock(uint32_t pid, int fd_cpu, const char* nombre, t_log* logger);

/*
 * Libera el mutex en nombre del proceso pid.
 * Si hay waiters, desencola el primero y le envía MSG_OK (lo desbloquea).
 * Devuelve 0 en éxito, -1 si el mutex no existe o pid no es el owner.
 */
int mutex_ks_unlock(uint32_t pid, const char* nombre, t_log* logger);

#endif
