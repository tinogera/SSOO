# Planificación Individual — Nicolas Alessandro Barreiro

## Módulos Asignados
- **Principal:** Utils (librería compartida), IO (módulo completo)
- **Secundario:** Kernel Scheduler — Mutex con herencia de prioridades, CMN (colas multinivel), compactación

## Ramas de trabajo

| Rama | Tareas |
|---|---|
| `feature/utils` | Wrapper de sockets, serialización/deserialización de mensajes |
| `feature/io` | Conexión a KScheduler, SLEEP, STDOUT, STDIN |
| `feature/kernel-scheduler/mutex-cmn` | Mutex sin herencia, CMN, QUEUE_PREEMPTION |
| `feature/kernel-scheduler/herencia-compactacion` | Planificada para herencia + compactación — finalmente incluida en la rama anterior |

> Crear `feature/utils` y `feature/io` desde `develop` al iniciar Fase 1. Las ramas de Scheduler se crean desde `develop` en Fase 2 y 3. En la práctica, herencia y compactación se implementaron directamente en `feature/kernel-scheduler/mutex-cmn` sin crear la segunda rama.

---

## Fase 0 — Configuración del Entorno
**Fecha límite:** 13/04/2026 — **COMPLETADA**

- [x] Instalar `so-commons-library` y verificar compilación de todos los módulos.
- [x] **Liderar el diseño del protocolo de comunicación** entre módulos: definir y documentar en Utils los tipos de mensaje, los structs compartidos y las convenciones de serialización que usarán todos los integrantes.
- [x] Crear el header compartido con los códigos de operación (enums de tipos de mensaje).
- [x] Coordinar con cada integrante los mensajes que necesita su módulo.

---

## Fase 1 — Check 1: Utils Base + IO Conexión
**Fecha límite:** 18/04/2026 — **ENTREGADO (parcial)**
> Check 1 entregado el 18/04 sin la parte de Luciano (Kernel Memory + Swap). Luciano completó sus módulos el 26/04. La parte de Nicolas estuvo completa a tiempo.

**Ramas:** `feature/utils` · `feature/io`

### Utils (13/04 – 18/04)
- [x] Implementar función **crear servidor**: crear socket, bind, listen, aceptar conexiones (bloqueante con pthread).
- [x] Implementar función **conectar a servidor**: crear socket, connect, retornar fd.
- [x] Implementar función **enviar mensaje**: serializar tipo + payload + tamaño, enviar por socket.
- [x] Implementar función **recibir mensaje**: recibir cabecera, leer payload de tamaño indicado, deserializar.
- [x] Publicar header `utils.h` con las firmas de todas las funciones para que los demás módulos puedan usar la librería.

### IO (13/04 – 18/04)
- [x] Implementar la conexión de **IO a Kernel Scheduler**: conectarse con el tipo de IO (STDIN/STDOUT/SLEEP) como argumento.
- [x] Implementar log: `## Conectado a Kernel Scheduler`.

---

## Fase 2 — Check 2: IO Completo + Mutex en Scheduler
**Fecha límite:** 23/05/2026 — **COMPLETADA** (merge a main el 22/05/2026, commit `29f8611`)
**Ramas:** `feature/io` (IO completo) · `feature/kernel-scheduler/mutex-cmn` (Mutex)

> Previo al desarrollo: se crearon y mergearon a develop las ramas `fix/protocolo-msg-cpu-km` (bug fix) y `feature/protocolo-msg-io-mutex` (op_codes y structs de payload para todos los mensajes de CK2).

### IO — Semanas 1–3 (19/04 – 09/05)
- [x] Implementar **IO tipo SLEEP** — _issue #14, cerrado 22/05_:
  - Recibir tiempo en ms desde Kernel Scheduler.
  - Ejecutar `usleep(tiempo * 1000)`.
  - Notificar a Kernel Scheduler que finalizó.
  - Implementar log: `## PID: <PID> - Haciendo sleep por <TIEMPO> milisegundos`.
  - Implementar log: `## PID: <PID> - Inicio de IO` y `## PID: <PID> - Fin de IO`.

- [x] Implementar **IO tipo STDOUT** — _issue #14, cerrado 22/05_:
  - Recibir cadena de bytes desde Kernel Scheduler.
  - Imprimir por pantalla (stdout).
  - Notificar a Kernel Scheduler que finalizó.
  - Implementar log: `## PID: <PID> - <CONTENIDO A IMPRIMIR>`.

- [x] Implementar **IO tipo STDIN** — _issue #14, cerrado 22/05_:
  - Recibir cantidad de bytes a leer.
  - Leer del teclado con `read()`.
  - Si entrada > bytes solicitados → cortar. Si entrada < bytes → rellenar con `\0`.
  - Enviar datos al Kernel Scheduler.
  - Implementar log: `## PID: <PID> - Ingrese <N> caracteres:`.

### Kernel Scheduler — Mutex sin herencia (10/05 – 23/05)
> Trabaja en conjunto con Santiago, quien maneja la estructura de colas.

- [x] Implementar **MUTEX_CREATE** — _issue cerrado 22/05_: estructura `t_ks_mutex` con nombre, `owner_pid` y cola de espera FIFO.
- [x] Implementar **MUTEX_LOCK**: si libre → tomar y responder MSG_OK. Si tomado → encolar waiter, CPU queda bloqueada esperando respuesta.
- [x] Implementar **MUTEX_UNLOCK**: liberar, si hay waiters → transferir al primero (FIFO) y responderle MSG_OK.
- [x] Implementar log: `## (<PID>) Toma el Mutex <NOMBRE>`.
- [x] Implementar log: `## (<PID>) Libera el Mutex <NOMBRE>`.

---

## Fase 3 — Check 3: CMN, Herencia de Prioridades y Compactación
**Fecha límite:** 20/06/2026 — **COMPLETADA** (merge a develop 18/06/2026, issues #65/#66/#67 finalizados 20/06/2026)
**Rama:** `feature/kernel-scheduler/mutex-cmn` (todo el trabajo de CK3 en esta rama)

### Ajustes v1.1 (completados primero, base para CK3)
- [x] `fix(cpu)`: quitar `esperar_ok_kernel` de MUTEX_CREATE, MUTEX_LOCK y MUTEX_UNLOCK en `cpu/src/cpu_syscalls.c` — commit `2e2fa9e`
- [x] `fix(ks)`: MUTEX_LOCK bloqueante mueve proceso a BLOCK en KS — commit `892d745`

### Semana 1–2 (24/05 – 06/06)
- [x] Implementar algoritmo **CMN (Colas Multinivel)** — commit `caa9672`:
  - N colas (configuradas en `QUEUES_ALGORITHMS`), cada una con su propio algoritmo (FIFO o RR).
  - Cada proceso tiene una prioridad que determina en qué cola se ubica (0 = mayor prioridad).
  - Siempre se despacha de la cola no vacía de mayor prioridad.
- [x] Implementar log: `## (<PID>) Prioridad: <PRIORIDAD_DESALOJADO> - Desalojado por cola más prioritaria por el proceso <PID> con prioridad <PRIORIDAD_NUEVA>`.

### Semana 3 (07/06 – 13/06)
- [x] Implementar **QUEUE_PREEMPTION** — commit `822b32d`: si un proceso de mayor prioridad llega a READY y hay un proceso de menor prioridad ejecutándose → enviar interrupción a CPU para desalojarlo → el desalojado vuelve al inicio de su cola READY.
- [x] Implementar **herencia de prioridades en Mutex** — commit `ea342fa`:
  - Si proceso de baja prioridad tiene un mutex tomado y un proceso de alta prioridad necesita ese mutex → proceso de baja prioridad hereda la prioridad del proceso bloqueado.
  - Al liberar el mutex → restaurar la prioridad original.
  - Implementar log: `## <PID> Cambio de prioridad: <ANTERIOR> - <NUEVA>`.

### Semana 4 (14/06 – 20/06)
- [x] Implementar el manejo de **compactación** en Kernel Scheduler — commit `89f4ba1`:
  - Cuando Kernel Memory indica que no hay espacio contiguo y es necesario compactar → desalojar todos los procesos de las CPUs.
  - Esperar a que todas las CPUs devuelvan el contexto.
  - Notificar a Kernel Memory para que compacte.
  - Una vez finalizada la compactación → reinsertar los procesos desalojados al **inicio** de su cola READY (caso excepcional).
  - Implementar log: `## Inicio de compactación` y `## Fin de compactación`.

### Ajustes por v1.1 del enunciado (08/06/2026)

- [x] **Syscalls liberan CPU sin esperar MSG_OK** (`cpu_syscalls.c`) — commit `2e2fa9e`:
  - Eliminar llamada a `esperar_ok_kernel` en `enviar_syscall_mutex_create` y `enviar_syscall_mutex_unlock`.
  - El KS ya no envía MSG_OK a la CPU para estas syscalls; redespacha al proceso por el planificador normal.

- [x] **MUTEX_LOCK bloqueante pasa al KS** (`ks_mutex.c`) — commit `892d745`:
  - Eliminado el `fd_cpu` del `t_mutex_waiter`; solo se guarda el PID.
  - Si el mutex está tomado: el proceso va a `BLOCK` en el KS (no se bloquea la CPU).
  - En `MUTEX_UNLOCK`: se saca el proceso de BLOCK y se pone en READY.

- [x] **Flujo real STDOUT** (`kernel_scheduler/src/main.c`) — _issue #65, commit `238fb10`_:
  - Al recibir `MSG_SYSCALL_STDOUT`: pedir bytes a KM con `MSG_LEER_DATOS {pid, dir_logica, tamanio}`.
  - Recibir `MSG_LEER_DATOS_RESP` con los bytes y reenviarlos a IO STDOUT como `[pid:4][bytes...]`.

- [x] **Flujo real STDIN** (`kernel_scheduler/src/main.c`) — _issue #66, commit `948b9c2`_:
  - Al recibir `MSG_SYSCALL_STDIN`: guardar `{pid, dir_logica, tamanio}` en `lista_stdin_pendientes`.
  - Al recibir `MSG_IO_STDIN_DATOS` de IO: recuperar `dir_logica`, enviar `MSG_ESCRIBIR_DATOS` a KM con los bytes leídos, mover proceso a READY.

- [x] **Syscalls MEM_ALLOC/MEM_FREE: reenviar a misma CPU** — _issue #67, commit `8d80525`_:
  - `sacar_proc_de_exec_para_mem()`: extrae el proceso de EXEC sin liberar la CPU.
  - Thread separado llama `km_request(MSG_CREAR_SEGMENTO / MSG_ELIMINAR_SEGMENTO)` y responde `MSG_OK/ERROR` al `fd_cpu` original.

---

## Fase 4 — Integración y Entrega Final
**Fecha límite:** 11/07/2026
**Rama:** `develop` (integración directa)

- [ ] Verificar todos los logs obligatorios de IO y Kernel Scheduler.
- [ ] Testear planificación CMN con múltiples niveles de prioridad.
- [ ] Testear herencia de prioridades en escenarios con varios mutex anidados.
- [ ] Testear compactación completa de punta a punta.
- [ ] Colaborar en la resolución de bugs de integración.

---

## Interfaces a acordar con otros integrantes

| Con quién | Qué acordar |
|---|---|
| **Todos** | Definir y publicar el protocolo de comunicación completo en Utils antes de la Fase 1 |
| **Santiago** | División dentro de Kernel Scheduler: Santiago → largo/mediano plazo, FIFO/RR; Nicolas → Mutex, CMN, herencia, compactación |
| **Kevin** | Mensajes Scheduler↔CPU para despacho, interrupciones y respuestas a syscalls |
| **Juan Manuel** | Mensajes de syscalls MEM_ALLOC/MEM_FREE desde CPU → Scheduler → KMemory |
