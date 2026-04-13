# Planificación General — Plug & Pray (1C 2026)

## Equipo: Impactante

| Integrante | GitHub |
|---|---|
| Kevin Luciano Castillo Panta | — |
| Santiago Gerardi | — |
| Luciano Lisachi | — |
| Juan Manuel Fernandez Vazquez | — |
| Nicolas Alessandro Barreiro | — |

---

## Cronograma de Checkpoints

| Hito | Fecha | Descripción |
|---|---|---|
| Check 1 | 18/04/2026 | Arquitectura y conexiones entre módulos |
| Check 2 | 23/05/2026 | Planificación FIFO/RR, IO, Mutex (sin herencia) |
| Check 3 | 20/06/2026 | CPU completa, segmentación, herencia de prioridades |
| Entrega Final | 11/07/2026 | Sistema completo integrado y testeado |

---

## Asignación de Módulos por Integrante

| Integrante | Módulos Principales | Módulos Secundarios |
|---|---|---|
| **Kevin** | CPU (ciclo instrucción, fetch/decode/execute) | Kernel Memory (contextos y gestión de instrucciones) |
| **Santiago** | Kernel Scheduler (largo y mediano plazo, FIFO/RR) | — |
| **Luciano** | Kernel Memory (segmentación, BEST/WORST FIT, compactación) | Swap |
| **Juan Manuel** | CPU (MMU, instrucciones de memoria) | Memory Stick |
| **Nicolas** | Utils + IO | Kernel Scheduler (Mutex, CMN, herencia, compactación) |

> Todos los integrantes participan en al menos uno de los módulos de mayor complejidad (Kernel Scheduler, Kernel Memory, CPU).

---

## Fases de Desarrollo

### Fase 0 — Configuración del Entorno
**Período:** 12/04 – 13/04/2026  
**Responsable coordinador:** Nicolas

- Instalación de `so-commons-library` en todas las máquinas.
- Verificación de compilación base de todos los módulos.
- Diseño y acuerdo del protocolo de comunicación entre módulos: tipos de mensaje, estructuras de datos compartidas, convenciones de serialización.
- Implementación inicial de Utils: funciones de sockets (servidor/cliente) y serialización.

---

### Fase 1 — Check 1: Conexiones Iniciales
**Período:** 13/04 – 18/04/2026  
**Objetivo:** Todos los módulos se conectan correctamente entre sí por sockets.

| Módulo | Tarea |
|---|---|
| Utils | Wrapper de sockets (conectar, escuchar, enviar, recibir) + serialización |
| Kernel Scheduler | Servidor de sockets: acepta Kernel Memory, CPUs e IOs |
| Kernel Memory | Servidor de sockets: acepta Scheduler, CPUs, Memory Sticks y Swap |
| CPU | Cliente: conecta a Kernel Scheduler y Kernel Memory |
| Memory Stick | Cliente a Kernel Memory + servidor para CPUs |
| Swap | Cliente a Kernel Memory, informa capacidad |
| IO | Cliente a Kernel Scheduler |

**Criterio de éxito:** Todos los módulos levantan, se conectan y loguean la conexión establecida.

---

### Fase 2 — Check 2: Planificación y Ciclo de CPU
**Período:** 19/04 – 23/05/2026  
**Objetivo:** Planificación básica funcionando de punta a punta con CPU ejecutando instrucciones simples.

| Módulo | Tareas |
|---|---|
| Kernel Scheduler | Estados NEW/READY/EXEC/BLOCK/EXIT, FIFO, RR, manejo de IO (SLEEP/STDIN/STDOUT), Mutex sin herencia |
| CPU | Registros, fetch, decode, instrucciones básicas (NOOP, SET, SUM, SUB, JNZ), syscalls, interrupciones |
| Kernel Memory | Retorno de instrucciones desde pseudocódigo, gestión de contextos mock |
| IO | SLEEP, STDOUT y STDIN completos |

**Criterio de éxito:** Un proceso corre de inicio a fin con instrucciones simples, planificación FIFO/RR correcta, IO funcional, Mutex sin herencia.

---

### Fase 3 — Check 3: CPU Completa y Segmentación
**Período:** 24/05 – 20/06/2026  
**Objetivo:** CPU con MMU completa, memoria segmentada real, planificación avanzada.

| Módulo | Tareas |
|---|---|
| CPU | MMU (dir. lógica → física), MOV_IN, MOV_OUT, COPY_MEM, MEM_ALLOC, MEM_FREE, INIT_PROC, EXIT, comunicación con Memory Sticks |
| Kernel Memory | Tabla de segmentos por PID, BEST/WORST FIT, creación/eliminación de segmentos, hot-plug de Memory Sticks, compactación, suspensión/des-suspensión a Swap |
| Kernel Scheduler | Suspensión/des-suspensión (mediano plazo), CMN (colas multinivel), QUEUE_PREEMPTION, herencia de prioridades, manejo de compactación, BSOD |
| Memory Stick | Implementación completa con MEMORY_DELAY |
| Swap | Implementación completa con bloques |

**Criterio de éxito:** Procesos con allocación de memoria real, MMU funcional, planificación CMN con desalojo, herencia de prioridades en mutex, suspensión por timeout.

---

### Fase 4 — Integración y Entrega Final
**Período:** 21/06 – 11/07/2026  
**Objetivo:** Sistema completo, integrado, con todos los logs obligatorios, testeado en entorno distribuido.

- Pruebas de integración con múltiples CPUs simultáneas.
- Pruebas de hot-plug dinámico de Memory Sticks.
- Verificación de todos los logs obligatorios en cada módulo.
- Configuración y prueba del script de deployment distribuido.
- Resolución de bugs de integración.

---

## Dependencias entre Módulos

```
Utils ──────────────────────── (base para todos)
Kernel Memory ← Kernel Scheduler ← CPU
Kernel Memory ← Swap
Kernel Memory ← Memory Sticks (hot-plug)
CPU → Memory Sticks (lectura/escritura)
Kernel Scheduler ← IO
```

**Orden de inicio del sistema:**
1. Memory Sticks
2. Swap
3. Kernel Memory
4. Kernel Scheduler
5. CPU(s)
6. IO(s)

---

## Riesgos y Mitigaciones

| Riesgo | Mitigación |
|---|---|
| Protocolo de comunicación inconsistente entre módulos | Definir y acordar structs en Utils antes de Fase 1 |
| Sincronización incorrecta en Kernel Scheduler (multihilo) | Usar mutexes de pthread en todas las colas compartidas |
| Bugs en MMU que bloqueen integración | Testear MMU aislada con valores conocidos antes de integrar |
| Segmentation Fault en CPU | Verificar límites de segmento en toda operación de memoria |
| Logs insuficientes en la entrega | Revisar lista de logs obligatorios en Fase 4 antes de entregar |

---

## Logs Obligatorios (resumen)

Cada módulo tiene logs obligatorios definidos en la consigna. La ausencia de cualquiera de estos logs implica desaprobación directa. Verificar antes de la entrega final que todos estén implementados.
