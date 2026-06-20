# Planificación Individual — Santiago Gerardi

## Módulos Asignados
- **Principal:** Kernel Scheduler — Planificación de largo plazo (estados y colas) y mediano plazo (suspensión/des-suspensión)

## Ramas de trabajo

| Rama | Tareas |
|---|---|
| `feature/kernel-scheduler/planificacion-basica` | Servidor de sockets, colas y estados, FIFO, RR, manejo de IO (SLEEP/STDIN/STDOUT) |
| `feature/kernel-scheduler/mediano-plazo` | Suspensión por timeout, des-suspensión, BSOD |

> Crear `feature/kernel-scheduler/planificacion-basica` desde `develop` al iniciar Fase 1. Crear `feature/kernel-scheduler/mediano-plazo` desde `develop` al iniciar Fase 3.

---

## Fase 0 — Configuración del Entorno
**Fecha límite:** 13/04/2026 — **COMPLETADA**

- [x] Instalar `so-commons-library` y verificar compilación del módulo `kernel_scheduler`.
- [x] Leer y entender completamente la sección de Kernel Scheduler en la consigna: estados de proceso, algoritmos de planificación, manejo de IO.
- [x] Coordinar con Nicolas el diseño del protocolo de mensajes que el Scheduler envía/recibe de CPUs, IO y Kernel Memory.

---

## Fase 1 — Check 1: Conexiones
**Fecha límite:** 18/04/2026 — **ENTREGADO (parcial)**
**Rama:** `feature/kernel-scheduler/planificacion-basica`

- [x] Implementar el **servidor de sockets** en Kernel Scheduler:
  - Conexión entrante desde **Kernel Memory** (establecida al inicio).
  - Conexiones dinámicas de **CPUs** (multihilo, una conexión por CPU).
  - Conexiones dinámicas de módulos **IO**.
- [x] Lograr que Kernel Scheduler levante, acepte conexiones y loguee cada conexión establecida.
- [x] Implementar el **log obligatorio**: `## Conectado a Kernel Memory`.
- [x] Implementar el **log obligatorio**: `## CPU <ID CPU> Conectada`.

**Dependencias:** Coordinarse con Nicolas para usar el wrapper de sockets de Utils. Coordinarse con Luciano para el handshake con Kernel Memory.

---

## Fase 2 — Check 2: Planificación Básica
**Fecha límite:** 23/05/2026 — **COMPLETADA** (merge a main el 22/05/2026, commit `29f8611`)
**Ramas:** `feature/kernel-scheduler/planificacion-basica` · `feature/ks-largo-plazo`

### Semana 1–2 (19/04 – 02/05)
- [x] Implementar la **estructura de proceso**: PID, estado, prioridad, cola de origen, tiempos.
- [x] Implementar las **colas de estado**: NEW, READY, EXEC, BLOCK, SUSP. BLOCK, SUSP. READY, EXIT.
- [x] Implementar la **creación de proceso**: leer path de archivo de instrucciones, asignar PID, notificar a Kernel Memory, pasar a NEW → READY.
- [x] Implementar log: `## (<PID>) Se crea el proceso - Estado: NEW`.
- [x] Implementar log: `## (<PID>) Pasa del estado <ANTERIOR> al estado <ACTUAL>`.

### Semana 3 (03/05 – 09/05)
- [x] Implementar algoritmo **FIFO**: despachar el proceso al tope de la cola READY a la CPU libre, sin preempción.
- [x] Implementar algoritmo **Round Robin (RR)**: igual que FIFO pero con quantum configurable (`RR_QUANTUM`). Al expirar el quantum, interrumpir la CPU y volver el proceso al final de READY.
- [x] Implementar envío de **interrupción de fin de quantum** a la CPU.
- [x] Implementar log: `## (<PID>) - Desalojado por fin de quantum`.

### Semana 4 (10/05 – 16/05)
- [x] Implementar el manejo de syscall **SLEEP**: proceso pasa a BLOCK, despachar siguiente proceso de READY a la CPU.
- [x] Implementar el manejo de syscalls **STDIN** y **STDOUT**: proceso pasa a BLOCK, solicitar operación al módulo IO correspondiente.
- [x] Al finalizar IO, volver proceso a READY (o SUSP. READY si está suspendido).
- [x] Implementar log: `## (<PID>) - Solicitó syscall: <NOMBRE_SYSCALL>`.
- [x] Implementar log: `## (<PID>) finalizó IO y pasa a READY / SUSP. READY`.

### Semana 5 (17/05 – 23/05) — adelantado a CK2
- [x] Implementar **MUTEX_CREATE/LOCK/UNLOCK** (hecho por Nicolas en `feature/kernel-scheduler/mutex-cmn`).
- [x] Implementar **SUSPENSION_TIMEOUT**: proceso en BLOCK supera timeout → SUSP. BLOCK (`thread_suspension_timer`).
- [x] Implementar des-suspensión simple: IO finaliza → SUSP. BLOCK → SUSP. READY → READY (`thread_largo_plazo`).
- [x] Implementar log: `## (<PID>) Toma el Mutex <NOMBRE_MUTEX>`.
- [x] Implementar log: `## (<PID>) Libera el Mutex <NOMBRE_MUTEX>`.

---

## Fase 3 — Check 3: Planificación de Mediano Plazo
**Fecha límite:** 20/06/2026
**Rama:** `feature/kernel-scheduler/mediano-plazo`

### Semana 1–2 (24/05 – 06/06)
- [x] ~~Suspensión por SUSPENSION_TIMEOUT~~ — adelantado a CK2 (`thread_suspension_timer`).
- [ ] Notificar a Kernel Memory para mover segmentos a Swap al suspender (pendiente CK3, requiere segmentación real).
- [ ] **Des-suspensión real**: verificar que los segmentos caben en memoria antes de des-suspender. Actualmente KM mockea espacio libre.

### Semana 3–4 (07/06 – 20/06)
- [ ] Implementar el manejo de **BSOD**: cuando Kernel Memory notifica desconexión de Memory Stick → finalizar todos los procesos activos → logear BSOD.
- [ ] Implementar el log de finalización: `## (<PID>) finalizó su ejecución con motivo de <MOTIVO>`.
- [ ] Testear escenarios completos de suspensión, des-suspensión y BSOD.
- [ ] Verificar correcta transición de estados en todos los escenarios posibles.

---

## Fase 4 — Integración y Entrega Final
**Fecha límite:** 11/07/2026
**Rama:** `develop` (integración directa)

- [ ] Integrar y testear el flujo completo con CPUs reales, IO real y Kernel Memory real.
- [ ] Verificar todos los logs obligatorios del Kernel Scheduler.
- [ ] Testear planificación con múltiples CPUs simultáneas.
- [ ] Colaborar en la resolución de bugs de integración.

---

## Interfaces a acordar con otros integrantes

| Con quién | Qué acordar |
|---|---|
| **Nicolas** | Formato de mensajes Scheduler↔IO, mensajes de resultado de IO. División: Santiago maneja largo/mediano plazo, Nicolas maneja CMN/herencia/compactación |
| **Kevin** | Protocolo de despacho de proceso a CPU (envío de PID, recepción de syscalls, envío de interrupciones) |
| **Luciano** | Protocolo Scheduler↔KMemory: creación de proceso, notificación de suspensión/des-suspensión, notificación de BSOD |
