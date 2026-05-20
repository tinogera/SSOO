# KS — Mutex Block Fix (Check 2)

**Fecha**: 2026-05-19
**Branch**: `kenrel-scheduler`
**Estado**: Diseño aprobado, pendiente implementación

---

## Problema

Cuando un proceso ejecuta `MUTEX_LOCK` sobre un mutex tomado, la consigna requiere que el proceso pase a **BLOCK** y la CPU quede libre para despachar otro proceso. La implementación actual no cumple ninguna de las dos cosas.

**Comportamiento actual** (`kernel_scheduler/src/ks_mutex.c:55-82`, `main.c:560-563`):
- `mutex_ks_lock` encolas un waiter con `(pid, fd_cpu)` y no responde.
- La CPU queda bloqueada en `recv` esperando `MSG_OK`.
- El proceso sigue conceptualmente en EXEC; la entrada de CPU sigue `ocupada = 1`.
- **Resultado**: la CPU no se libera, el planificador no puede usarla, se rompe la concurrencia.

## Decisión: Opción B (devolución estilo syscall)

`MUTEX_LOCK` se trata como una syscall que **siempre** desaloja al proceso de EXEC. El destino (READY o BLOCK) depende del estado del mutex.

### Por qué B y no A o C
- **A** (CPU espera en recv): es el modelo actual, intrínsecamente roto — no hay forma de liberar físicamente la CPU mientras esperás un OK.
- **C** (nuevo op_code `MSG_MUTEX_BLOQUEADO`): más eficiente en el caso común pero requiere extender el protocolo y coordinar contrato nuevo con Kevin.
- **B**: reusa el patrón que Kevin ya implementa para SLEEP/STDOUT/STDIN, no toca el protocolo, deja el código del scheduler consistente. Trade-off aceptado: caso "mutex libre" tiene un round-trip extra (proc va a READY y vuelve a EXEC vía planificador). No crítico para este TP.

---

## Contrato con CPU (Kevin)

Al ejecutar la instrucción `MUTEX_LOCK <nombre>`, la CPU debe:

1. Enviar `MSG_MUTEX_LOCK` con `{pid, nombre}` — **sin esperar respuesta**.
2. Enviar `MSG_DEVOLVER_PROCESO` con `{pid, motivo=MOTIVO_DEVOLUCION_SYSCALL, pc=<instrucción siguiente al lock>}`.
3. Esperar el próximo `MSG_DESPACHAR_PROCESO`.

`MUTEX_CREATE` y `MUTEX_UNLOCK` siguen siendo **síncronos** (CPU envía y espera `MSG_OK`, sigue ejecutando el mismo proceso — no devuelve).

Cuando la CPU recibe `MSG_DESPACHAR_PROCESO` después de haber sido bloqueada por mutex, el PC ya apunta a la instrucción siguiente al lock (porque la CPU avanzó el PC antes de devolver). El proceso ya tiene el mutex tomado conceptualmente.

---

## Cambios en KS

### `ks_mutex.h` / `ks_mutex.c` — API redesign

**`t_mutex_waiter`** pierde `fd_cpu` (no necesario, el proc se redespacha vía planificador):
```c
typedef struct {
    uint32_t pid;
} t_mutex_waiter;
```

**`mutex_ks_lock`** deja de tener `fd_cpu` y deja de mandar `MSG_OK`. El caller decide qué hacer con el proceso según el retorno:
```c
// Retorna:
//   0  → mutex libre, lo tomó pid (log "Toma el Mutex" ya emitido)
//   1  → mutex tomado, pid encolado como waiter
//  -1  → error (mutex no existe)
int mutex_ks_lock(uint32_t pid, const char* nombre, t_log* logger);
```

**`mutex_ks_unlock`** deja de mandar `MSG_OK` al fd del waiter; en cambio, devuelve el PID del próximo waiter para que el caller lo mueva BLOCK→READY:
```c
// Retorna 0 en éxito, -1 en error.
// out_next_waiter_pid:
//   ≥0 → próximo waiter promovido a owner (log "Toma el Mutex" ya emitido)
//   -1 → no había waiters
int mutex_ks_unlock(uint32_t pid, const char* nombre, t_log* logger,
                    int* out_next_waiter_pid);
```

### `main.c` — handlers de mutex en `atender_cpu`

**`MSG_MUTEX_CREATE`**: sin cambios (sigue siendo sync).

**`MSG_MUTEX_LOCK`** (nuevo flujo):
```c
case MSG_MUTEX_LOCK: {
    t_payload_mutex* p = msg->payload;
    uint32_t pid = ntohl(p->pid);

    t_proceso* proc = sacar_de_exec(pid);
    if (!proc) break;  // defensa: proc ya no estaba en EXEC

    int r = mutex_ks_lock(pid, p->nombre, logger);
    if (r == 0) {
        // libre — va a READY, planificador lo redespacha
        cambiar_estado(proc, READY);
        pthread_mutex_lock(&mutex_ready);
        queue_push(cola_ready, proc);
        pthread_mutex_unlock(&mutex_ready);
        sem_post(&sem_cpu_disponible);
    } else if (r == 1) {
        // tomado — va a BLOCK
        mover_a_block(proc);
    }
    break;
}
```

**`MSG_MUTEX_UNLOCK`** (nuevo flujo):
```c
case MSG_MUTEX_UNLOCK: {
    t_payload_mutex* p = msg->payload;
    uint32_t pid = ntohl(p->pid);
    int next_pid = -1;

    mutex_ks_unlock(pid, p->nombre, logger, &next_pid);
    enviar_mensaje(fd, MSG_OK, NULL, 0);  // responder al unlocker (CPU sigue ejecutando)

    if (next_pid >= 0) {
        // sacar al waiter de BLOCK y mandarlo a READY
        t_proceso* waiter = sacar_de_block(next_pid);
        if (waiter) {
            cambiar_estado(waiter, READY);
            pthread_mutex_lock(&mutex_ready);
            queue_push(cola_ready, waiter);
            pthread_mutex_unlock(&mutex_ready);
            sem_post(&sem_cpu_disponible);
        }
    }
    break;
}
```

### `MSG_DEVOLVER_PROCESO` motivo `SYSCALL`

Sin cambios — el handler actual ya hace nada para este motivo (comentario explícito en `main.c`: "la CPU devuelve el proceso después de que el KS ya lo movió a BLOCK en el handler de la syscall"). Aplica igual para LOCK.

---

## Logs

Cumple los logs obligatorios:
- `## (PID) Toma el Mutex N` — emitido en `mutex_ks_lock` caso libre, y en `mutex_ks_unlock` cuando promueve un waiter.
- `## (PID) Libera el Mutex N` — emitido en `mutex_ks_unlock`.
- `## (PID) Pasa del estado EXEC al estado BLOCK` — emitido por `cambiar_estado` en `mover_a_block`.
- `## (PID) Pasa del estado BLOCK al estado READY` — emitido cuando se promueve un waiter.

No se añaden logs nuevos.

---

## Tests

`kernel_scheduler/tests/test_ks_mutex.c` debe actualizarse: las signaturas de `mutex_ks_lock` y `mutex_ks_unlock` cambian. Cobertura mínima a mantener:

1. `create` único — éxito.
2. `create` duplicado — falla.
3. `lock` sobre mutex libre — devuelve 0, owner queda seteado.
4. `lock` sobre mutex tomado — devuelve 1, waiter queda encolado.
5. `unlock` por owner sin waiters — owner queda libre, `out_next_waiter_pid = -1`.
6. `unlock` por owner con waiter — owner pasa a ser el waiter, `out_next_waiter_pid = pid del waiter`.
7. `unlock` por no-owner — error.
8. FIFO de múltiples waiters — el orden de promoción respeta el orden de encolado.

Lo que se quita: cualquier assertion sobre `MSG_OK` enviado por `mutex_ks_lock`/`unlock`, porque dejaron de mandarlo.

---

## Alcance / no-alcance

**Entra:**
- Refactor de `ks_mutex.h/c` con la nueva API.
- Reescritura de handlers `MSG_MUTEX_LOCK` y `MSG_MUTEX_UNLOCK` en `main.c`.
- Update de tests unitarios.

**No entra (Check 3):**
- Suspensión de procesos en BLOCK por timeout.
- Herencia de prioridades.
- Recovery si la CPU se cae con un mutex tomado.

---

## Riesgos

1. **Kevin tiene que adaptar la CPU**: si no devuelve el proceso después de `MUTEX_LOCK`, queda colgado esperando un `MSG_OK` que nunca llega. Documentar el contrato y avisar antes del CK2.
2. **Round-trip extra en caso "mutex libre"**: aceptado conscientemente. Si en CK3 se observa que duele, se puede migrar a Opción C agregando un op_code de respuesta.
