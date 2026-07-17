#ifndef KS_QUEUE_SEARCH_H_
#define KS_QUEUE_SEARCH_H_

#include <commons/collections/queue.h>
#include "proceso.h"

/*
 * Busca (sin sacar) un t_proceso por PID dentro de una t_queue, recorriendo
 * su lista interna sin modificarla ni la cola ni el orden de sus elementos.
 * No es thread-safe: el llamador es responsable de tomar el mutex que
 * protege esa cola antes de invocarla (igual que ya hace buscar_proceso_activo
 * en main.c con mutex_exec / mutex_colas_ready[nivel] / mutex_block).
 * Devuelve el puntero al proceso encontrado o NULL si no está en la cola.
 */
t_proceso* buscar_pid_en_queue(t_queue* cola, int pid);

#endif
