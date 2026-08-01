#ifndef KS_MUTEX_H_
#define KS_MUTEX_H_

#include <stdint.h>
#include <pthread.h>
#include <commons/collections/list.h>
#include <commons/collections/queue.h>
#include <commons/log.h>

/*
 * Entrada de la cola de espera de un mutex.
 * Guardamos PID y prioridad: cuando el waiter se convierte en owner
 * su prioridad se usa como nuevo owner_prioridad_original.
 */
typedef struct {
    uint32_t pid;
    int      prioridad;
} t_mutex_waiter;

/*
 * Representa un mutex del sistema.
 * owner_pid == -1 indica que el mutex está libre.
 * owner_prioridad_original: prioridad del owner antes de cualquier herencia;
 *   se restaura cuando el owner libera el mutex.
 * cola_espera contiene t_mutex_waiter* en orden FIFO.
 */
typedef struct {
    char*           nombre;
    int             owner_pid;
    int             owner_prioridad_original;
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
 * Intenta tomar el mutex en nombre del proceso pid con prioridad dada.
 * - Si está libre: toma el mutex, loguea y devuelve 0.
 * - Si está tomado: encola al waiter y devuelve 1.
 *   Si el waiter tiene mayor prioridad que el owner (prioridad < owner_prioridad_original),
 *   activa herencia: *owner_a_elevar = PID del owner, *nueva_prioridad_owner = prioridad del waiter.
 *   El llamador debe elevar la prioridad del owner y loguear el cambio.
 * Devuelve -1 si el mutex no existe.
 * En caso de no herencia, *owner_a_elevar = -1.
 */
int mutex_ks_lock(uint32_t pid, int prioridad, const char* nombre, t_log* logger,
                  int* owner_a_elevar, int* nueva_prioridad_owner);

/*
 * Libera el mutex en nombre del proceso pid.
 * Si hay waiters, desencola el primero y lo convierte en el nuevo owner.
 * Devuelve el PID del nuevo owner (>= 0) si había waiter, -1 si no había.
 * *prioridad_restaurar: prioridad original del owner antes de cualquier herencia.
 *   El llamador debe restaurar la prioridad del owner (pid) a este valor si cambió.
 *   Es -1 si el mutex no existía o pid no era el owner.
 */
int mutex_ks_unlock(uint32_t pid, const char* nombre, t_log* logger,
                    int* prioridad_restaurar);

/*
 * Prioridad más alta (número más chico) entre los waiters de TODOS los mutex
 * que posee pid. Devuelve INT_MAX si pid no posee mutex o ninguno tiene cola.
 *
 * Es la pieza que faltaba para que la herencia sea correcta con más de un
 * mutex: mirando un solo mutex, soltarlo restauraría la prioridad base aunque
 * otro mutex retenido siga teniendo un waiter más prioritario.
 */
int mutex_ks_prioridad_heredada(uint32_t pid);

#endif
