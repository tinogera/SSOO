#include "ks_queue_search.h"

#include <commons/collections/list.h>

t_proceso* buscar_pid_en_queue(t_queue* cola, int pid) {
    int sz = queue_size(cola);
    for (int i = 0; i < sz; i++) {
        t_proceso* p = list_get(cola->elements, i);
        if (p->PID == pid) return p;
    }
    return NULL;
}
