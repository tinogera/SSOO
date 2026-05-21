# Informe Kernel Scheduler — Bugs pendientes para CK2
## Rama: `kenrel-scheduler` — 20/05/2026

---

## Contexto

El KS está mayoritariamente implementado y compila sin errores. Planificación FIFO/RR,
despacho, manejo de IO y mutex están completos. Quedan **dos bugs/faltantes puntuales**
que bloquean el flujo completo con el CPU de Kevin.

---

## Bug 1 — `main.c:426`: op_code incorrecto para SLEEP 🔴

### Código actual
```c
case MSG_IO_SLEEP: {          // op_code 8 — este es KS → IO, no CPU → KS
    t_payload_io_sleep* p = msg->payload;
    ...
```

### Por qué rompe

El CPU envía `MSG_SYSCALL_SLEEP` (op_code **25**) cuando el proceso ejecuta la
instrucción SLEEP. El KS tiene handler para `MSG_IO_SLEEP` (op_code **8**), que es
el mensaje que el KS le manda *al módulo IO*, no el que recibe del CPU.

Cuando llega el SLEEP del CPU (op_code 25) cae al `default` de la línea 504 →
loguea warning → el proceso queda en EXEC sin moverse a BLOCK → el CPU nunca recibe
respuesta → **deadlock**.

### Fix — cambiar una sola línea (main.c:426)

```c
// Cambiar:
case MSG_IO_SLEEP: {

// Por:
case MSG_SYSCALL_SLEEP: {
```

El resto del handler es correcto: usa `t_payload_io_sleep` que tiene los mismos
campos que `t_payload_syscall_sleep` (`pid` + `tiempo_ms`), y el forward al
módulo IO con `MSG_IO_SLEEP` está bien.

---

## Faltante 2 — sin handler `MSG_SYSCALL_EXIT` (op_code 28) 🔴

### Por qué rompe

Cuando el proceso ejecuta EXIT, el CPU hace dos cosas en orden:
1. Envía `MSG_SYSCALL_EXIT` (28) → espera `MSG_OK`
2. Envía `MSG_DEVOLVER_PROCESO` (18) con `MOTIVO_DEVOLUCION_EXIT`

Sin el handler, el `MSG_SYSCALL_EXIT` cae al `default` → KS no responde →
**el CPU queda bloqueado esperando el `MSG_OK` que nunca llega**.

El handler de `MOTIVO_DEVOLUCION_EXIT` en `MSG_DEVOLVER_PROCESO` (línea 404) ya
existe y está bien, pero nunca llega porque el CPU está colgado en el paso 1.

### Fix — agregar el case en `atender_cpu` (antes del `default` en la línea 504)

```c
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
```

**Nota**: cuando después llegue `MSG_DEVOLVER_PROCESO` con `MOTIVO_DEVOLUCION_EXIT`,
`sacar_de_exec(pid)` retorna NULL (el proceso ya salió de EXEC) y el handler
hace `break` sin efectos secundarios. No hay problema de doble procesamiento.

---

## Faltante 3 — sin handler `MSG_INIT_PROC` (op_code 16) 🟡

### Por qué importa

La instrucción `INIT_PROC archivo prioridad` del lenguaje del TP hace que el CPU
envíe `MSG_INIT_PROC` al KS para crear un proceso hijo. Sin handler, si algún
script de prueba usa `INIT_PROC`, cae al `default` y el CPU queda colgado esperando
respuesta.

Si los scripts de prueba del CK2 no usan `INIT_PROC`, este faltante no bloquea.

### Fix — agregar el case antes del `default`

El payload de `MSG_INIT_PROC` es: `{ uint32_t pid_padre, char path[], uint32_t prioridad }`.

```c
case MSG_INIT_PROC: {
    if (msg->payload_size < sizeof(uint32_t)) {
        enviar_mensaje(fd, MSG_ERROR, NULL, 0);
        break;
    }

    // pid_padre (no lo usamos por ahora, CK3)
    // path viene después del pid
    char* path_hijo = (char*)msg->payload + sizeof(uint32_t);

    // prioridad al final del string (después del '\0')
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
```

---

## Resumen de acciones

| # | Archivo | Línea | Cambio | Tiempo est. |
|---|---------|-------|--------|-------------|
| 1 | `kernel_scheduler/src/main.c` | 426 | `MSG_IO_SLEEP` → `MSG_SYSCALL_SLEEP` | 1 min |
| 2 | `kernel_scheduler/src/main.c` | ~503 | Agregar `case MSG_SYSCALL_EXIT` | 10 min |
| 3 | `kernel_scheduler/src/main.c` | ~503 | Agregar `case MSG_INIT_PROC` (si hay scripts que lo usan) | 15 min |

Una vez aplicados, mergear `kenrel-scheduler` → `develop`.

---

## Estado general del módulo (referencia)

| Componente | Estado |
|-----------|--------|
| Infraestructura (colas, mutexes, semáforos, tabla CPUs) | ✅ |
| `crear_proceso` + envío a KM | ✅ |
| Planificador FIFO | ✅ |
| Planificador RR + timer de quantum | ✅ |
| Despacho a CPU (`MSG_DESPACHAR_PROCESO`) | ✅ |
| `MSG_DEVOLVER_PROCESO` con motivo EXIT | ✅ |
| `MSG_DEVOLVER_PROCESO` con motivo INTERRUPCION | ✅ |
| `MSG_DEVOLVER_PROCESO` con motivo SYSCALL | ✅ |
| Syscall SLEEP (`MSG_SYSCALL_SLEEP`) | 🔴 Bug op_code (fix de 1 línea) |
| Syscall STDOUT (`MSG_SYSCALL_STDOUT`) | ✅ |
| Syscall STDIN (`MSG_SYSCALL_STDIN`) | ✅ |
| Syscall EXIT (`MSG_SYSCALL_EXIT`) | 🔴 Sin handler |
| Desbloqueo por `MSG_IO_FIN` / `MSG_IO_STDIN_DATOS` | ✅ |
| Mutex CREATE / LOCK / UNLOCK | ✅ |
| Todos los logs obligatorios | ✅ |
| `MSG_INIT_PROC` (instrucción INIT_PROC) | 🟡 Sin handler (CK2 si hay scripts) |
