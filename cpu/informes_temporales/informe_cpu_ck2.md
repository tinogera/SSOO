# Informe CPU — Bugs pendientes para CK2
## Rama: `impactante` — 19/05/2026

---

## Contexto

Se realizó una revisión completa del módulo CPU. El ciclo fetch→decode→execute
está bien estructurado, el manejo de interrupciones es correcto, y el protocolo
de contexto con KM ya fue corregido (MSG_RESTAURAR_CONTEXTO / MSG_GUARDAR_CONTEXTO).

Quedan **dos bugs críticos** que impiden que el CPU ejecute cualquier proceso en CK2.

---

## Bug 1 — `main.c`: no recibe MSG_OK del KS tras identificación 🔴

### Ubicación
`cpu/src/main.c`, líneas 47–51

### Código actual
```c
uint32_t size;
void* payload = serializar_string(cpu_id, &size);
enviar_mensaje(socket_kernel, MSG_CPU_IDENTIFICACION, payload, size);
free(payload);
// ← acá falta recibir la respuesta del KS
```

### Por qué rompe

El Kernel Scheduler envía `MSG_OK` (op_code 3) como respuesta a la identificación.
El CPU nunca lo consume. Cuando el loop principal llama a `recibir_proceso_a_ejecutar()`,
recibe ese `MSG_OK` en lugar de `MSG_DESPACHAR_PROCESO` (op_code 17).

`cpu_dispatch.c:13` detecta que el op_code no es 17, loguea error y retorna `false`.
El while loop de `main.c:76` corta. **El CPU termina sin haber ejecutado ningún proceso.**

### Fix

Agregar después de `free(payload)` en `main.c:50`:

```c
t_mensaje* resp_ks = recibir_mensaje(socket_kernel);
if (resp_ks == NULL || resp_ks->op_code != MSG_OK) {
    log_error(logger, "Kernel Scheduler rechazo identificacion");
    socket_kernel = -1;
} else {
    log_info(logger, "## Conectado a Kernel Scheduler");
}
free_mensaje(resp_ks);
```

---

## Bug 2 — `cpu_fetch.c`: pid y pc enviados sin conversión de byte order 🔴

### Ubicación
`cpu/src/cpu_fetch.c`, líneas 12–15

### Código actual
```c
t_payload_fetch_instruccion pedido = {
    .pid = pid,
    .pc  = registros->pc
};
```

### Por qué rompe

KM recibe el payload de `MSG_FETCH_INSTRUCCION` usando `deserializar_fetch_request()`
(en `utils/src/utils/sockets.c`), que aplica `ntohl()` a ambos campos:

```c
req->pid = ntohl(pid_n);
req->pc  = ntohl(pc_n);
```

En una máquina little-endian (x86_64), el entero `pid = 1` está en memoria como
`0x01 0x00 0x00 0x00`. KM lo interpreta como big-endian → `0x01000000` = 16.777.216.

KM busca el archivo de instrucciones del proceso 16.777.216 → no lo encuentra →
retorna `MSG_ERROR` → el CPU falla en el primer fetch de cada proceso.

### Nota sobre los otros payloads

Los payloads de syscalls (`SLEEP`, `STDOUT`, `STDIN`, `EXIT`, `MUTEX_*`) y de
despacho/devolución (`MSG_DESPACHAR_PROCESO`, `MSG_DEVOLVER_PROCESO`) **no tienen
este problema**: KS tampoco usa `ntohl`/`htonl` en ninguno de esos casos, así que
ambos lados son consistentes y funcionan en la misma máquina.

### Fix

Agregar `#include <arpa/inet.h>` al inicio de `cpu_fetch.c` y cambiar el payload:

```c
#include <arpa/inet.h>   // agregar

t_payload_fetch_instruccion pedido = {
    .pid = htonl(pid),
    .pc  = htonl(registros->pc)
};
```

---

## Resumen

| # | Archivo | Línea | Descripción | Tiempo est. |
|---|---------|-------|-------------|-------------|
| 1 | `cpu/src/main.c` | 50 | Agregar recepción de MSG_OK del KS post-identificación | 5 min |
| 2 | `cpu/src/cpu_fetch.c` | 12–15 | Agregar `htonl()` en pid y pc del payload de fetch | 2 min |

Ambos fixes son mecánicos. Con esto el CPU debería poder ejecutar procesos end-to-end para CK2.

---

## Estado general del módulo (post-fix contexto)

| Componente | Estado |
|-----------|--------|
| Conexión y handshake con KM | ✅ |
| Conexión y handshake con KS | 🔴 Falta recibir MSG_OK |
| Fetch de instrucción (MSG_FETCH_INSTRUCCION) | 🔴 Bug byte order |
| Decode de instrucciones CK2 | ✅ |
| Execute: NOOP, SET, SUM, SUB, JNZ | ✅ |
| Syscall SLEEP (MSG_SYSCALL_SLEEP 25) | ✅ |
| Syscall STDOUT (MSG_SYSCALL_STDOUT 26) | ✅ |
| Syscall STDIN (MSG_SYSCALL_STDIN 27) | ✅ |
| Syscall EXIT (MSG_SYSCALL_EXIT 28) | ✅ |
| Mutex CREATE/LOCK/UNLOCK (13–15) | ✅ |
| Restaurar contexto desde KM | ✅ (corregido en este merge) |
| Guardar contexto en KM | ✅ (corregido en este merge) |
| Detección de interrupción de quantum | ✅ |
| Devolución de proceso a KS | ✅ |
