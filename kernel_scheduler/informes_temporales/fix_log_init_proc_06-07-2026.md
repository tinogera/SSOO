# Fix: log "Solicitó syscall: INIT_PROC" — 06/07/2026

**Módulo:** Kernel Scheduler  
**Archivo:** `kernel_scheduler/src/main.c`  
**Commit:** `1ed8a79`

## Problema

El handler `MSG_INIT_PROC` en `atender_cpu()` no emitía el log obligatorio del enunciado:

```
## (<PID>) - Solicitó syscall: INIT_PROC
```

Todos los demás syscalls (SLEEP, STDOUT, STDIN, MUTEX_CREATE, MUTEX_LOCK, MUTEX_UNLOCK, MEM_ALLOC, MEM_FREE, EXIT) ya tenían su log con ese formato. INIT_PROC era el único que faltaba.

## Causa

El handler usaba el `pid_padre` del payload únicamente como campo reservado sin leerlo. Al no extraerlo, no había forma de incluir el PID en el log y el `log_info` no fue agregado en su momento.

## Fix aplicado

Se agregó la extracción explícita de `pid_padre` del primer `uint32_t` del payload (presente pero no utilizado antes) y el log inmediatamente antes de llamar a `crear_proceso`:

```c
uint32_t pid_padre_n;
memcpy(&pid_padre_n, msg->payload, sizeof(uint32_t));
int pid_padre = (int)ntohl(pid_padre_n);

log_info(logger, "## (%d) - Solicitó syscall: INIT_PROC", pid_padre);
t_proceso* hijo = crear_proceso(path_hijo, prioridad);
```

## Verificación

El módulo compila sin warnings. El log se emite usando el PID del proceso padre (quien invocó la syscall), que es el valor correcto según el enunciado.
