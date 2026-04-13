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

## Estrategia de Branching

### Estructura de ramas

```
main
└── develop
      ├── feature/utils
      ├── feature/io
      ├── feature/swap
      ├── feature/memory-stick
      ├── feature/cpu/ciclo-basico
      ├── feature/cpu/mmu
      ├── feature/kernel-memory/conexiones-instrucciones
      ├── feature/kernel-memory/segmentacion
      ├── feature/kernel-memory/suspension-compactacion
      ├── feature/kernel-scheduler/planificacion-basica
      ├── feature/kernel-scheduler/mediano-plazo
      ├── feature/kernel-scheduler/mutex-cmn
      └── feature/kernel-scheduler/herencia-compactacion
```

### Reglas de branching

- **`main`**: solo recibe merges desde `develop` en cada checkpoint. Siempre debe compilar y funcionar.
- **`develop`**: rama de integración. Cada feature branch se mergea aquí al completarse.
- **`feature/*`**: una rama por módulo o agrupación lógica de tareas. Se crea al inicio de la fase correspondiente y se mergea a `develop` al terminar.
- No hacer commits directamente en `main` ni en `develop`.
- Resolver conflictos en la feature branch antes de mergear a `develop`.

### Asignación de ramas por integrante

| Rama | Responsable |
|---|---|
| `feature/utils` | Nicolas |
| `feature/io` | Nicolas |
| `feature/kernel-scheduler/planificacion-basica` | Santiago |
| `feature/kernel-scheduler/mediano-plazo` | Santiago |
| `feature/kernel-scheduler/mutex-cmn` | Nicolas |
| `feature/kernel-scheduler/herencia-compactacion` | Nicolas |
| `feature/kernel-memory/conexiones-instrucciones` | Luciano + Kevin |
| `feature/kernel-memory/segmentacion` | Luciano |
| `feature/kernel-memory/suspension-compactacion` | Luciano |
| `feature/cpu/ciclo-basico` | Kevin |
| `feature/cpu/mmu` | Juan Manuel |
| `feature/memory-stick` | Juan Manuel |
| `feature/swap` | Luciano |

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

| Módulo | Tarea | Rama |
|---|---|---|
| Utils | Wrapper de sockets (conectar, escuchar, enviar, recibir) + serialización | `feature/utils` |
| Kernel Scheduler | Servidor de sockets: acepta Kernel Memory, CPUs e IOs | `feature/kernel-scheduler/planificacion-basica` |
| Kernel Memory | Servidor de sockets: acepta Scheduler, CPUs, Memory Sticks y Swap | `feature/kernel-memory/conexiones-instrucciones` |
| CPU | Cliente: conecta a Kernel Scheduler y Kernel Memory | `feature/cpu/ciclo-basico` |
| Memory Stick | Cliente a Kernel Memory + servidor para CPUs | `feature/memory-stick` |
| Swap | Cliente a Kernel Memory, informa capacidad | `feature/swap` |
| IO | Cliente a Kernel Scheduler | `feature/io` |

> Al llegar al Check 1 se mergean todas las feature branches activas a `develop` y luego `develop` → `main`.

**Criterio de éxito:** Todos los módulos levantan, se conectan y loguean la conexión establecida.

---

### Fase 2 — Check 2: Planificación y Ciclo de CPU
**Período:** 19/04 – 23/05/2026  
**Objetivo:** Planificación básica funcionando de punta a punta con CPU ejecutando instrucciones simples.

| Módulo | Tareas | Rama |
|---|---|---|
| Kernel Scheduler | Estados NEW/READY/EXEC/BLOCK/EXIT, FIFO, RR, manejo de IO (SLEEP/STDIN/STDOUT) | `feature/kernel-scheduler/planificacion-basica` |
| Kernel Scheduler | Mutex sin herencia | `feature/kernel-scheduler/mutex-cmn` |
| CPU | Registros, fetch, decode, instrucciones básicas (NOOP, SET, SUM, SUB, JNZ), syscalls, interrupciones | `feature/cpu/ciclo-basico` |
| Kernel Memory | Retorno de instrucciones desde pseudocódigo, gestión de contextos mock | `feature/kernel-memory/conexiones-instrucciones` |
| IO | SLEEP, STDOUT y STDIN completos | `feature/io` |

> Al llegar al Check 2: mergear a `develop` y luego `develop` → `main`.

**Criterio de éxito:** Un proceso corre de inicio a fin con instrucciones simples, planificación FIFO/RR correcta, IO funcional, Mutex sin herencia.

---

### Fase 3 — Check 3: CPU Completa y Segmentación
**Período:** 24/05 – 20/06/2026  
**Objetivo:** CPU con MMU completa, memoria segmentada real, planificación avanzada.

| Módulo | Tareas | Rama |
|---|---|---|
| CPU | MMU (dir. lógica → física), MOV_IN, MOV_OUT, COPY_MEM, MEM_ALLOC, MEM_FREE, comunicación con Memory Sticks | `feature/cpu/mmu` |
| CPU | INIT_PROC, EXIT | `feature/cpu/ciclo-basico` |
| Kernel Memory | Tabla de segmentos, BEST/WORST FIT, creación/eliminación de segmentos, hot-plug | `feature/kernel-memory/segmentacion` |
| Kernel Memory | Compactación, suspensión/des-suspensión a Swap | `feature/kernel-memory/suspension-compactacion` |
| Kernel Scheduler | Suspensión/des-suspensión (mediano plazo), BSOD | `feature/kernel-scheduler/mediano-plazo` |
| Kernel Scheduler | CMN, QUEUE_PREEMPTION, Mutex | `feature/kernel-scheduler/mutex-cmn` |
| Kernel Scheduler | Herencia de prioridades, compactación | `feature/kernel-scheduler/herencia-compactacion` |
| Memory Stick | Implementación completa con MEMORY_DELAY | `feature/memory-stick` |
| Swap | Implementación completa con bloques | `feature/swap` |

> Al llegar al Check 3: mergear a `develop` y luego `develop` → `main`.

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
