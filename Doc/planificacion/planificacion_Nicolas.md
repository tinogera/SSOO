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
| `feature/kernel-scheduler/herencia-compactacion` | Herencia de prioridades, manejo de compactación |

> Crear `feature/utils` y `feature/io` desde `develop` al iniciar Fase 1 (Nicolas es el primero en crear ramas porque Utils es dependencia de todos). Crear las ramas de Scheduler desde `develop` en Fase 2 y 3 respectivamente.

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
**Fecha límite:** 23/05/2026 — **EN CURSO** (arrancó 29/04/2026)
**Ramas:** `feature/io` (IO completo) · `feature/kernel-scheduler/mutex-cmn` (Mutex)

> Previo al desarrollo: se crearon y mergearon a develop las ramas `fix/protocolo-msg-cpu-km` (bug fix) y `feature/protocolo-msg-io-mutex` (op_codes y structs de payload para todos los mensajes de CK2).

### IO — Semanas 1–3 (19/04 – 09/05)
- [x] Implementar **IO tipo SLEEP** — _issue #23, cerrado 29/04_ (commit `321d7c4`):
  - Recibir tiempo en ms desde Kernel Scheduler.
  - Ejecutar `usleep(tiempo * 1000)`.
  - Notificar a Kernel Scheduler que finalizó.
  - Implementar log: `## PID: <PID> - Haciendo sleep por <TIEMPO> milisegundos`.
  - Implementar log: `## PID: <PID> - Inicio de IO` y `## PID: <PID> - Fin de IO`.

- [ ] Implementar **IO tipo STDOUT** — _issue #24_:
  - Recibir cadena de bytes desde Kernel Scheduler.
  - Imprimir por pantalla (stdout).
  - Notificar a Kernel Scheduler que finalizó.
  - Implementar log: `## PID: <PID> - <CONTENIDO A IMPRIMIR>`.

- [ ] Implementar **IO tipo STDIN** — _issue #25_:
  - Recibir cantidad de bytes a leer.
  - Leer del teclado (con `readline` o `fgets`).
  - Si entrada > bytes solicitados → cortar. Si entrada < bytes → rellenar con `\0`.
  - Enviar datos al Kernel Scheduler para que los escriba en memoria.
  - Implementar log: `## PID: <PID> - Ingrese <N> caracteres:`.

### Kernel Scheduler — Mutex sin herencia (10/05 – 23/05)
> Trabaja en conjunto con Santiago, quien maneja la estructura de colas.

- [ ] Implementar **MUTEX_CREATE** — _issue #15_: crear estructura de mutex con nombre dado, estado libre/tomado, cola de espera.
- [ ] Implementar **MUTEX_LOCK**: si el mutex está libre → tomarlo, loguear. Si está tomado → poner proceso en BLOCK esperando ese mutex.
- [ ] Implementar **MUTEX_UNLOCK**: liberar el mutex, si hay procesos esperando → desbloquear el primero (política FIFO dentro de la cola de espera del mutex), loguear.
- [ ] Implementar log: `## (<PID>) Toma el Mutex <NOMBRE>`.
- [ ] Implementar log: `## (<PID>) Libera el Mutex <NOMBRE>`.

---

## Fase 3 — Check 3: CMN, Herencia de Prioridades y Compactación
**Fecha límite:** 20/06/2026
**Ramas:** `feature/kernel-scheduler/mutex-cmn` (CMN, QUEUE_PREEMPTION) · `feature/kernel-scheduler/herencia-compactacion` (herencia, compactación)

### Semana 1–2 (24/05 – 06/06)
- [ ] Implementar algoritmo **CMN (Colas Multinivel)**:
  - N colas (configuradas en `QUEUES_ALGORITHMS`), cada una con su propio algoritmo (FIFO o RR).
  - Cada proceso tiene una prioridad que determina en qué cola se ubica (0 = mayor prioridad).
  - Siempre se despacha de la cola no vacía de mayor prioridad.
- [ ] Implementar log: `## (<PID>) Prioridad: <PRIORIDAD_DESALOJADO> - Desalojado por cola más prioritaria por el proceso <PID> con prioridad <PRIORIDAD_NUEVA>`.

### Semana 3 (07/06 – 13/06)
- [ ] Implementar **QUEUE_PREEMPTION**: si un proceso de mayor prioridad llega a READY y hay un proceso de menor prioridad ejecutándose → enviar interrupción a CPU para desalojarlo → el desalojado vuelve al inicio de su cola READY.
- [ ] Implementar **herencia de prioridades en Mutex**:
  - Si proceso de baja prioridad tiene un mutex tomado y un proceso de alta prioridad necesita ese mutex → proceso de baja prioridad hereda la prioridad del proceso bloqueado.
  - Al liberar el mutex → restaurar la prioridad original.
  - Implementar log: `## <PID> Cambio de prioridad: <ANTERIOR> - <NUEVA>`.

### Semana 4 (14/06 – 20/06)
- [ ] Implementar el manejo de **compactación** en Kernel Scheduler:
  - Cuando Kernel Memory indica que no hay espacio contiguo y es necesario compactar → desalojar todos los procesos de las CPUs.
  - Esperar a que todas las CPUs estuelvan el contexto.
  - Notificar a Kernel Memory para que compacte.
  - Una vez finalizada la compactación → reinsertar los procesos desalojados al **inicio** de su cola READY (caso excepcional).
  - Implementar log: `## Inicio de compactación` y `## Fin de compactación`.

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
