# Documentación completa — Módulo Kernel Scheduler

**TP Cuatrimestral "Plug & Pray" — Sistemas Operativos, UTN FRBA — 1C 2026**
**Grupo:** Impactante
**Módulo documentado:** `kernel_scheduler/`
**Enunciado de referencia:** `Doc/Plug & Pray v1.1.md`

---

## Índice

1. [Introducción: qué es el TP y qué rol cumple el Kernel Scheduler](#1-introducción)
2. [Arquitectura general del sistema](#2-arquitectura-general-del-sistema)
3. [Marco teórico](#3-marco-teórico)
   - 3.1 [Procesos y el modelo de 7 estados](#31-procesos-y-el-modelo-de-7-estados)
   - 3.2 [Niveles de planificación: largo, mediano y corto plazo](#32-niveles-de-planificación)
   - 3.3 [Algoritmos de planificación: FIFO, Round Robin y Colas Multinivel](#33-algoritmos-de-planificación)
   - 3.4 [Syscalls, interrupciones y el ciclo de instrucción](#34-syscalls-interrupciones-y-el-ciclo-de-instrucción)
   - 3.5 [Entrada/Salida (I/O) y bloqueo de procesos](#35-entradasalida-io)
   - 3.6 [Sincronización: mutex, deadlock, inversión y herencia de prioridades](#36-sincronización-mutex-deadlock-inversión-y-herencia-de-prioridades)
   - 3.7 [Gestión de memoria: segmentación, paginación, fragmentación y compactación](#37-gestión-de-memoria)
   - 3.8 [Memoria virtual, swapping y suspensión de procesos](#38-memoria-virtual-swapping-y-suspensión)
4. [Qué pedía el enunciado para el Kernel Scheduler](#4-qué-pedía-el-enunciado)
5. [Implementación práctica](#5-implementación-práctica)
   - 5.1 [Estructura de archivos del módulo](#51-estructura-de-archivos)
   - 5.2 [Estructuras de datos centrales](#52-estructuras-de-datos-centrales)
   - 5.3 [Modelo de concurrencia: los hilos del KS](#53-modelo-de-concurrencia)
   - 5.4 [Protocolo de comunicación](#54-protocolo-de-comunicación)
   - 5.5 [Arranque del módulo (`main`)](#55-arranque-del-módulo)
   - 5.6 [Planificación de largo plazo](#56-planificación-de-largo-plazo)
   - 5.7 [Planificación de corto plazo](#57-planificación-de-corto-plazo)
   - 5.8 [Planificación de mediano plazo: suspensión y des-suspensión](#58-planificación-de-mediano-plazo)
   - 5.9 [Atención de syscalls](#59-atención-de-syscalls)
   - 5.10 [Mutex y herencia de prioridades](#510-mutex-y-herencia-de-prioridades)
   - 5.11 [Desalojo por compactación](#511-desalojo-por-compactación)
   - 5.12 [BSOD — Blue Screen of Death](#512-bsod)
   - 5.13 [El patrón `km_request` / `thread_km_listener`](#513-el-patrón-km_request--thread_km_listener)
6. [Problemas encontrados y cómo se resolvieron](#6-problemas-encontrados-y-soluciones)
7. [Decisiones de diseño](#7-decisiones-de-diseño)
8. [Logs obligatorios](#8-logs-obligatorios)
9. [Archivo de configuración](#9-archivo-de-configuración)
10. [Testing](#10-testing)
11. [Observaciones y deuda técnica conocida](#11-observaciones-y-deuda-técnica-conocida)

---

## 1. Introducción

El TP "Plug & Pray" consiste en construir un **sistema operativo distribuido simulado**, repartido en seis módulos que son procesos Linux independientes y se comunican exclusivamente por **sockets TCP**. La gracia del enunciado (de ahí el nombre) es que los componentes de hardware se pueden "enchufar en caliente": los Memory Sticks (la RAM simulada) pueden conectarse y desconectarse durante la ejecución, y el sistema tiene que reaccionar — ampliando la memoria disponible o muriendo con un *Blue Screen of Death* si un stick con datos desaparece.

Dentro de esa arquitectura, el **Kernel Scheduler (KS)** es el corazón de la **planificación de procesos**: decide qué proceso ejecuta en qué CPU, en qué orden, cuándo un proceso se bloquea, cuándo se suspende a disco, cuándo vuelve, y atiende todas las **syscalls** que los procesos generan mientras corren. Es, en términos de un SO real, el *scheduler* + el *dispatcher* + la capa de atención de llamadas al sistema.

Este documento cubre tanto la **teoría** detrás de cada mecanismo (planificación multinivel, deadlock, herencia de prioridades, segmentación vs. paginación, swapping) como la **práctica**: qué pedía exactamente el enunciado, cómo lo implementamos, con qué estructuras, con qué hilos, qué bugs encontramos en el camino y cómo los resolvimos.

---

## 2. Arquitectura general del sistema

```
                        ┌──────────────────┐
                        │  Kernel Memory   │◄─────────┐
                        │      (KM)        │          │
                        └───┬─────┬────┬───┘          │
             conexión KS→KM │     │    │              │
        (crear proc, susp., │     │    │ bloques      │ lecturas/escrituras
         leer/escribir      │     │    ▼              │ físicas
         datos, segmentos)  │     │  ┌──────┐         │
                            │     │  │ Swap │    ┌────┴─────────┐
                            │     │  └──────┘    │ Memory Stick │ (1..N,
┌──────────────────┐        │     │              │   (MS)       │  hot-plug)
│ Kernel Scheduler │◄───────┘     │ fetch de     └──────────────┘
│      (KS)        │              │ instrucciones        ▲
└──┬────────────┬──┘              │ y contexto           │ MOV_IN/MOV_OUT
   │            │                 │                      │
   │ despacho / │ pedidos IO   ┌──┴───┐                  │
   │ syscalls   │              │ CPU  │──────────────────┘
   ▼            ▼              │(1..N)│
┌──────┐   ┌─────────┐         └──────┘
│ CPU  │   │   IO    │
│(1..N)│   │ SLEEP / │
└──────┘   │ STDIN / │
           │ STDOUT  │
           └─────────┘
```

Roles de cada módulo:

| Módulo | Rol |
|---|---|
| **Kernel Scheduler** | Planificación de largo/mediano/corto plazo, atención de syscalls, gestión de mutexes, coordinación de compactación y BSOD. **Servidor** de CPUs e IOs; **cliente** de KM. |
| **Kernel Memory (KM)** | Gestión de la memoria segmentada: tablas de segmentos por proceso, asignación Best/Worst Fit, compactación, suspensión a Swap, servicio de instrucciones (fetch) y contextos. |
| **CPU** | Ejecuta el ciclo Fetch → Decode → Execute. Traduce direcciones lógicas con su MMU. Devuelve el proceso al KS ante syscall, interrupción, EXIT o segmentation fault. |
| **Memory Stick (MS)** | La "RAM física": un bloque de bytes con retardo configurable por acceso. Se pueden conectar varios en caliente. |
| **Swap** | Dispositivo de bloques respaldado en archivo, donde KM guarda los segmentos de los procesos suspendidos. |
| **IO** | Dispositivos de E/S: una instancia por tipo (SLEEP, STDIN, STDOUT). |
| **utils** | Biblioteca estática compartida: framing de mensajes, serialización y el protocolo (`op_code`s y payloads). |

**Orden de arranque obligatorio:** KM → MS → Swap → **KS** → CPUs → IOs. El KS necesita a KM vivo antes de crear el PID 0, y las CPUs/IOs se conectan al KS cuando este ya tiene su servidor levantado.

El KS se lanza así:

```
./bin/kernel_scheduler [Archivo Config] [Path Proceso Inicial]
```

El segundo argumento es el script del **PID 0**, el proceso inicial del sistema, que siempre tiene **prioridad máxima (0)** y desde el cual —vía la syscall `INIT_PROC`— se crean todos los demás procesos.

---

## 3. Marco teórico

Esta sección explica los conceptos de la materia que el módulo pone en práctica. Cada apartado cierra con una nota de **"cómo aplica en nuestro TP"**.

### 3.1 Procesos y el modelo de 7 estados

Un **proceso** es un programa en ejecución: el código más su contexto (registros, program counter, memoria asignada, estado de planificación). El sistema operativo necesita saber en todo momento *qué puede hacer* cada proceso, y para eso lo modela como una **máquina de estados**.

El modelo clásico de 5 estados (NEW, READY, RUNNING, BLOCKED, EXIT) se extiende a **7 estados** cuando el SO soporta **suspensión** (swapping de procesos a disco):

```
                          ┌────────────────────────────────┐
                          ▼                                │
 NEW ──────► READY ────► EXEC ────► EXIT                   │
              ▲  ▲        │                                │
              │  │        │ syscall bloqueante             │ fin de IO /
              │  │        ▼         (IO, mutex tomado)     │ desalojo
              │  │      BLOCK                              │
              │  │        │                                │
              │  │        │ timeout de suspensión          │
              │  │        ▼                                │
              │  │   SUSP. BLOCK                           │
              │  │        │                                │
              │  │        │ fin de IO / mutex liberado     │
              │  │        ▼                                │
              │  └── SUSP. READY                           │
              │        (des-suspensión: vuelve             │
              └──────── a READY si hay memoria)────────────┘
```

- **NEW**: el proceso fue creado pero todavía no fue admitido en el sistema.
- **READY**: listo para ejecutar; espera que el planificador le asigne una CPU.
- **EXEC** (running): está ejecutando en una CPU.
- **BLOCK**: espera un evento externo (fin de una IO, liberación de un mutex). No puede usar CPU aunque haya una libre.
- **SUSP. BLOCK**: sigue bloqueado, pero además **su memoria fue movida a disco** (swap) para liberar RAM. Es un proceso "congelado en el freezer".
- **SUSP. READY**: el evento que esperaba ya ocurrió, pero su memoria sigue en disco: no puede ejecutar hasta que el SO la traiga de vuelta.
- **EXIT**: terminó (normalmente, por error, o por decisión del sistema).

La distinción BLOCK / SUSP. BLOCK es lo que le da al SO una palanca para el **grado de multiprogramación**: si un proceso va a estar bloqueado mucho tiempo, ocupar RAM con él es un desperdicio; mejor mandarlo a disco y darle esa memoria a procesos que sí pueden avanzar.

**En nuestro TP:** el KS implementa exactamente este modelo. Cada estado tiene su cola (`cola_new`, `colas_ready[]`, `cola_exec`, `cola_block`, `cola_susp_block`, `cola_susp_ready`, `cola_exit`) y toda transición se loguea con el formato obligatorio `## (<PID>) Pasa del estado X al estado Y`. El pasaje BLOCK → SUSP. BLOCK ocurre por timeout (`SUSPENSION_TIMEOUT` del config) y dispara el movimiento real de los segmentos del proceso al módulo Swap, coordinado con KM.

### 3.2 Niveles de planificación

La teoría distingue tres planificadores según *cada cuánto* actúan y *qué* deciden:

**Planificador de largo plazo (job scheduler).** Decide qué procesos son **admitidos** al sistema (NEW → READY). Controla el grado de multiprogramación: cuántos procesos compiten simultáneamente por CPU y memoria. En sistemas batch clásicos era clave; en sistemas interactivos modernos casi no existe (todo se admite al toque).

**Planificador de mediano plazo (swapper).** Decide qué procesos se **suspenden** (se van a disco) y cuáles se **reanudan** (vuelven a RAM). Es el que maneja las transiciones BLOCK ↔ SUSP. BLOCK y SUSP. READY → READY. Su objetivo es balancear el uso de memoria: si la RAM está saturada de procesos bloqueados, los suspende; cuando hay espacio de nuevo, los des-suspende.

**Planificador de corto plazo (CPU scheduler / dispatcher).** Decide, cada vez que una CPU queda libre (o cuando corresponde desalojar), **qué proceso de READY pasa a EXEC**. Es el que corre con más frecuencia (milisegundos) y por eso debe ser rápido. El componente que efectivamente realiza el cambio de contexto —cargar los registros del proceso elegido en la CPU— se llama **dispatcher**.

**En nuestro TP:**
- **Largo plazo:** como los procesos nacen *sin memoria asignada* (los segmentos se crean recién con `MEM_ALLOC`), el enunciado permite pasar NEW → READY sin restricción. Nuestra implementación hace la transición inmediatamente dentro de `crear_proceso()`. Además, `thread_largo_plazo` reintenta des-suspensiones pendientes (ver 5.6 y 5.8).
- **Mediano plazo:** `thread_suspension_timer` (un hilo por proceso que entra a BLOCK) implementa la suspensión por timeout, y `manejar_mas_memoria()` implementa la des-suspensión ordenada por prioridad y antigüedad cuando KM avisa que hay más memoria.
- **Corto plazo:** `thread_planificador` es el dispatcher central: espera con un semáforo a que haya (proceso en READY + CPU libre) y despacha según el algoritmo configurado.

### 3.3 Algoritmos de planificación

Un algoritmo de planificación de corto plazo define **el orden** en que los procesos de READY obtienen la CPU, y si un proceso en ejecución **puede ser desalojado** (preemptive) o no (non-preemptive). Los criterios de evaluación clásicos: uso de CPU, throughput, tiempo de retorno (turnaround), tiempo de espera, tiempo de respuesta, y fairness (que nadie sufra *starvation*, inanición).

#### FIFO (First In, First Out / FCFS)

El más simple: los procesos se atienden en orden de llegada a READY, y **no hay desalojo** — el proceso ejecuta hasta que se bloquea o termina.

- ✔ Trivial de implementar, sin overhead, sin starvation (todos eventualmente llegan al frente).
- ✘ **Efecto convoy**: un proceso largo de CPU retrasa a todos los cortos que llegaron detrás. Pésimo tiempo de respuesta para cargas interactivas.

#### Round Robin (RR)

FIFO + **quantum**: cada proceso recibe una tajada fija de tiempo de CPU (el quantum, `Q`). Si al agotarse `Q` el proceso sigue ejecutando, se lo **desaloja por interrupción de reloj** y vuelve al final de READY.

- ✔ Excelente tiempo de respuesta; reparte la CPU equitativamente; sin starvation.
- ✘ Elegir `Q` es un trade-off: `Q` muy chico → demasiados cambios de contexto (overhead); `Q` muy grande → degenera en FIFO. Regla práctica: `Q` debe ser mayor que la duración de la mayoría de las ráfagas de CPU.

#### Colas Multinivel (CMN)

Se definen **N colas de READY, una por nivel de prioridad**. Cada cola puede tener su propio algoritmo interno (FIFO o RR en nuestro caso). El planificador siempre elige de la **cola no vacía de mayor prioridad**; las colas de menor prioridad solo ejecutan cuando las superiores están vacías.

Dos variantes importantes:

- **Sin retroalimentación** (nuestro caso): la prioridad de un proceso es fija — nunca cambia de cola por su comportamiento. (Las colas multinivel *retroalimentadas*, MLFQ, mueven procesos entre colas según cuánto consumen la CPU; el enunciado explícitamente las excluye.)
- **Desalojo entre colas (preemption)**: si llega a READY un proceso de prioridad mayor que uno que está ejecutando, ¿lo desaloja? Si el desalojo está habilitado, sí — el menos prioritario vuelve a READY (al frente de su cola, para no perder su turno) y el más prioritario toma la CPU.

El riesgo teórico de las prioridades fijas es la **inanición** de los niveles bajos: si siempre hay procesos de prioridad alta, los de prioridad baja no ejecutan nunca. Los SO reales lo mitigan con *aging* (envejecer la prioridad). El enunciado no lo pide; asumimos que las pruebas de la cátedra no generan starvation patológica.

**En nuestro TP:** el algoritmo se elige por config (`PLANIFICATION_ALGORITHM = FIFO | RR | CMN`).
- FIFO y RR se modelan como el caso degenerado de CMN con **una sola cola** (`n_colas = 1`), lo que unificó el código del planificador.
- Con CMN, `QUEUES_ALGORITHMS=[FIFO,RR,...]` define el algoritmo de cada nivel (índice 0 = prioridad máxima) y `QUEUE_PREEMPTION=TRUE|FALSE` habilita el desalojo entre colas. Solo se planifican prioridades dentro del rango definido (con N colas, prioridades 0..N-1; una prioridad fuera de rango se ajusta a la última cola como defensa).
- El quantum de RR se implementa con un hilo-timer por despacho (ver 5.7).
- En FIFO y RR la prioridad **no tiene injerencia alguna** (enunciado): todos caen en la única cola.

### 3.4 Syscalls, interrupciones y el ciclo de instrucción

En un SO real, un proceso de usuario **no puede** hacer I/O, pedir memoria ni sincronizarse con otros procesos por sí solo: esas operaciones requieren privilegios de kernel. La **syscall** (llamada al sistema) es el mecanismo controlado para cruzar esa frontera: el proceso ejecuta una instrucción especial (trap/int) que transfiere el control al kernel en modo privilegiado, el kernel atiende el pedido, y devuelve el control (al mismo proceso, o a otro si el pedido lo bloqueó).

Las **interrupciones** son el mecanismo dual, iniciado por hardware o por el propio kernel: un timer, un dispositivo que terminó, u otra CPU. Para el planificador, la interrupción de reloj es la herramienta que hace posible el **desalojo** (preemption): sin ella, un proceso podría quedarse con la CPU para siempre.

La CPU ejecuta el **ciclo de instrucción**: *Fetch* (buscar la instrucción que apunta el PC) → *Decode* (interpretarla) → *Execute* (ejecutarla) → *Check Interrupt* (¿llegó una interrupción? si sí, guardar contexto y devolver el control al kernel).

**En nuestro TP** la frontera usuario/kernel se simula con la separación de módulos:
- La **CPU** (módulo) ejecuta el ciclo: hace *fetch* de la instrucción a KM (que la lee del script del proceso, indexado por PC), decodifica y ejecuta. Al final de cada ciclo chequea si el KS le mandó una interrupción (`MSG_INTERRUPCION_CPU`).
- Las instrucciones que la CPU **no puede resolver sola** son syscalls y viajan al KS por socket: `SLEEP`, `STDIN`, `STDOUT`, `MUTEX_CREATE/LOCK/UNLOCK`, `MEM_ALLOC`, `MEM_FREE`, `INIT_PROC`, `EXIT`. Para simplificar la lectura de los scripts, cada syscall es una instrucción con nombre propio (a diferencia de la vida real, donde hay una única instrucción de trap con un número de syscall).
- El "cambio de contexto" está repartido: la CPU guarda/restaura el contexto (registros + PC) contra KM (`MSG_GUARDAR_CONTEXTO` / `MSG_RESTAURAR_CONTEXTO`), y el KS solo maneja el PID y el estado de planificación.
- Las interrupciones que el KS envía a la CPU llevan un motivo: `MOTIVO_INTERRUPCION_QUANTUM` (fin de quantum RR) o `MOTIVO_INTERRUPCION_DESALOJO` (preemption entre colas, compactación, BSOD).
- La CPU devuelve el proceso con `MSG_DEVOLVER_PROCESO {pid, motivo, pc}` donde el motivo distingue: `SYSCALL` (syscall no bloqueante atendida), `EXIT`, `ERROR` (segmentation fault detectado por la MMU), `INTERRUPCION` (fue desalojada).

### 3.5 Entrada/Salida (I/O)

Los dispositivos de E/S son órdenes de magnitud más lentos que la CPU. Si un proceso pide una I/O y la CPU se quedara esperando el resultado (*busy waiting*), se desperdiciaría muchísimo cómputo. La solución clásica es la **I/O bloqueante con planificación**:

1. El proceso pide la I/O (syscall).
2. El kernel lo pasa a BLOCK y lanza la operación en el dispositivo.
3. La CPU se asigna a **otro proceso** de READY (¡acá está la ganancia de la multiprogramación!).
4. Cuando el dispositivo termina, interrumpe al kernel, que pasa el proceso a READY.

Este solapamiento CPU/I/O es la razón de existir de los estados BLOCK y del planificador: mantener la CPU siempre ocupada con trabajo útil.

**En nuestro TP** hay tres dispositivos, cada uno un proceso `io` independiente conectado al KS:

- **SLEEP**: recibe `{pid, tiempo_ms}`, duerme ese tiempo y responde `MSG_IO_FIN {pid}`. Simula un dispositivo genérico con latencia.
- **STDOUT**: recibe `{pid, bytes}` y los imprime por pantalla. Los bytes salen de la memoria del proceso: el KS se los pide a KM antes de reenviarlos.
- **STDIN**: recibe `{pid, n_bytes}`, le pide al usuario que tipee esa cantidad de caracteres y los devuelve (`MSG_IO_STDIN_DATOS`); el KS los escribe en la memoria del proceso vía KM.

En los tres casos el flujo de planificación es idéntico al teórico: al llegar la syscall el proceso pasa **EXEC → BLOCK**, el KS libera la CPU y despacha el siguiente proceso de READY; al llegar el fin de IO el proceso pasa **BLOCK → READY** (o **SUSP. BLOCK → SUSP. READY** si mientras tanto fue suspendido — ver 5.8).

### 3.6 Sincronización: mutex, deadlock, inversión y herencia de prioridades

#### Exclusión mutua y mutex

Cuando varios procesos comparten un recurso, las **secciones críticas** (el código que toca el recurso compartido) deben ejecutarse de a una por vez, o los resultados dependen del orden de intercalado (*race conditions*). El **mutex** (mutual exclusion) es la primitiva mínima: un candado que solo un proceso puede tener tomado; los demás que lo pidan quedan **bloqueados** hasta que el dueño lo libere. Es equivalente a un semáforo binario con la restricción de que solo el que lo tomó puede liberarlo.

Para que la espera sea justa (sin inanición), la cola de espera del mutex suele ser **FIFO**: los bloqueados se despiertan en el orden en que pidieron el candado.

#### Deadlock (interbloqueo)

Un **deadlock** es una situación en la que un conjunto de procesos queda bloqueado para siempre porque cada uno espera un recurso que tiene otro miembro del conjunto. Las cuatro **condiciones de Coffman**, todas necesarias para que exista deadlock:

1. **Exclusión mutua**: los recursos no son compartibles (un mutex tomado no lo puede tomar otro).
2. **Hold and wait**: un proceso retiene recursos mientras espera otros.
3. **No preemption de recursos**: no se le puede sacar un recurso a un proceso por la fuerza.
4. **Espera circular**: existe un ciclo P1 → P2 → ... → Pn → P1 donde cada uno espera un recurso del siguiente.

Estrategias generales:
- **Prevención**: romper estructuralmente alguna condición (p. ej., imponer un orden total de adquisición de recursos elimina la espera circular).
- **Evitación**: decidir en cada asignación si el sistema queda en estado seguro (algoritmo del banquero). Caro y requiere conocimiento a priori.
- **Detección y recuperación**: dejar que ocurra, detectar el ciclo (grafo de asignación de recursos) y matar/retroceder procesos.
- **Ignorarlo** (algoritmo del avestruz): lo que hace la mayoría de los SO de propósito general con los recursos de usuario — si tus programas se deadlockean, es tu problema.

**En nuestro TP** hay dos planos de deadlock, y es importante no confundirlos:

- **Deadlock entre procesos simulados** (por `MUTEX_LOCK` cruzados en los scripts): el KS **no lo detecta ni lo previene** — adopta la estrategia del avestruz, igual que el enunciado, que no pide detección. Si dos scripts toman `M1`/`M2` en orden cruzado, ambos quedarán en BLOCK para siempre (y eventualmente en SUSP. BLOCK por el timeout de suspensión). Es responsabilidad de quien escribe los scripts.
- **Deadlock interno del propio KS** (entre nuestros hilos y sockets): este sí fue un problema real de implementación y nos pasó **tres veces** durante el desarrollo, siempre con la misma forma: *un hilo espera por socket una respuesta que solo él mismo podría leer*. Los tres casos y sus fixes están detallados en la sección 6 — son el mejor ejemplo práctico del TP de "hold and wait + espera circular", solo que los recursos eran el socket con KM y el hilo listener.

#### Inversión de prioridades y herencia

En un planificador con prioridades aparece un problema sutil cuando se mezclan prioridades con mutexes:

1. **L** (prioridad baja) toma el mutex `M`.
2. **H** (prioridad alta) intenta tomar `M` → se bloquea esperando a L.
3. **M** (prioridad media, sin relación con el mutex) llega a READY y, por tener más prioridad que L, **le gana la CPU a L**.

Resultado: **H, el proceso más prioritario del sistema, está de hecho esperando a M, uno de prioridad media**. Eso es la **inversión de prioridades**. No es un deadlock (eventualmente se destraba), pero puede demorar a H de forma no acotada. Es un problema famoso en la vida real: la sonda **Mars Pathfinder (1997)** se reseteaba en Marte por exactamente esto, y se arregló habilitando remotamente la herencia de prioridades en su mutex.

La solución clásica es la **herencia de prioridades**: mientras L tenga tomado un mutex por el que espera un proceso más prioritario, L **hereda temporalmente la prioridad del más prioritario de sus waiters**. Así M ya no puede colarse: L ejecuta con prioridad de H, termina su sección crítica rápido, libera el mutex y **recupera su prioridad original**.

**En nuestro TP** la herencia está implementada en `ks_mutex.c` + el handler de `MSG_MUTEX_LOCK`/`MSG_MUTEX_UNLOCK` (detalle en 5.10): al bloquear a un waiter más prioritario que el owner, se eleva la prioridad del owner (con el log obligatorio de cambio de prioridad); al liberar el mutex, el owner recupera su prioridad original. Nota: como en CMN la prioridad define *la cola*, elevar la prioridad del owner hace que en sus próximos pasos por READY entre a una cola más alta, y con `QUEUE_PREEMPTION` puede incluso desalojar a otros. La herencia solo tiene efecto observable bajo CMN — en FIFO/RR la prioridad no juega.

### 3.7 Gestión de memoria

El TP usa **segmentación pura**, pero para entender por qué eso trae compactación y qué alternativa existe, conviene tener las dos teorías.

#### Direcciones lógicas vs. físicas y la MMU

Los procesos trabajan con **direcciones lógicas** (su propio espacio, empezando en 0); la memoria real usa **direcciones físicas**. La **MMU** (Memory Management Unit) traduce unas en otras en cada acceso. Esta indirección es la base de la protección (un proceso no puede tocar memoria ajena) y de la reubicación (el SO puede mover un proceso en RAM sin que este se entere).

#### Segmentación

La memoria del proceso se divide en **segmentos**: unidades de tamaño **variable** con significado lógico (código, datos, stack — o, en nuestro caso, lo que el programa pida con `MEM_ALLOC`). Cada proceso tiene una **tabla de segmentos**; cada entrada guarda `(base, límite)`: dónde arranca el segmento en memoria física y cuánto mide.

Traducción de una dirección lógica:

```
nro_segmento   = dir_logica / SEGMENT_MAX_SIZE
desplazamiento = dir_logica % SEGMENT_MAX_SIZE

si desplazamiento >= limite[nro_segmento]  →  SEGMENTATION FAULT
dir_fisica = base[nro_segmento] + desplazamiento
```

(En la teoría general el número de segmento viene en los bits altos de la dirección; nuestro TP lo define aritméticamente con un tamaño máximo de segmento fijo por config, que es equivalente.)

- ✔ Refleja la estructura lógica del programa; protección y compartición a nivel de segmento; el chequeo de límite da el clásico **segfault**.
- ✘ Como los segmentos son de tamaño variable, al crearlos y destruirlos la memoria libre queda cortada en huecos: **fragmentación externa**.

#### Fragmentación externa, algoritmos de huecos y compactación

**Fragmentación externa**: hay suficiente memoria libre *total* para un pedido, pero ningún hueco *contiguo* lo suficientemente grande. Es el problema estructural de cualquier esquema de particiones variables (segmentación incluida).

Para elegir en qué hueco poner un segmento nuevo:

- **First Fit**: el primer hueco que alcance. Rápido.
- **Best Fit**: el hueco más chico que alcance. Minimiza el sobrante inmediato, pero genera muchos huecos minúsculos e inutilizables.
- **Worst Fit**: el hueco más grande. Deja sobrantes grandes y potencialmente reutilizables.

Cuando la fragmentación externa impide una asignación que *cabría* en el total libre, la salida es la **compactación**: mover todos los segmentos ocupados hacia un extremo de la memoria, juntando todos los huecos en uno solo. Es carísima (hay que copiar memoria y actualizar todas las bases en todas las tablas de segmentos) y exige que **ningún proceso esté ejecutando** mientras se mueven sus segmentos — si una CPU tradujera una dirección con una base vieja a mitad de la mudanza, leería basura.

**En nuestro TP:** KM implementa Best Fit / Worst Fit (elegible por config `ALLOCATION_STRATEGY`) sobre un espacio de direcciones físicas global que concatena todos los Memory Sticks conectados. Cuando un `MEM_ALLOC` no encuentra hueco contiguo pero el espacio total alcanza, KM le pide al **KS** que desaloje todas las CPUs (`MSG_COMPACTAR`), compacta, y recién entonces reintenta. El rol del KS en esa coreografía —frenar el mundo y avisar cuándo está quieto— está detallado en 5.11.

#### Paginación (teoría — la alternativa que este TP no usa)

La **paginación** divide la memoria física en **marcos** (frames) de tamaño fijo y el espacio lógico en **páginas** del mismo tamaño. La tabla de páginas mapea página → marco. Como todas las piezas son iguales, **cualquier página cabe en cualquier marco libre**:

- ✔ **Elimina por completo la fragmentación externa** → nunca hace falta compactar.
- ✔ Base natural para memoria virtual por demanda (page faults, reemplazo LRU/Clock, etc.).
- ✘ Aparece **fragmentación interna**: la última página de cada proceso desperdicia en promedio media página.
- ✘ Las tablas de páginas son grandes (se resuelven con tablas multinivel) y cada acceso lógico requiere un acceso extra a la tabla (se resuelve con la **TLB**, un caché de traducciones).
- ✘ Se pierde la visión "lógica" de la memoria: una página no significa nada para el programa.

Los sistemas reales históricos combinaron ambas (**segmentación paginada**: segmentos cuyo interior se pagina — Multics, x86 de 32 bits), quedándose con la semántica de segmentos y sin fragmentación externa. Los SO modernos sobre x86-64 usan paginación pura con segmentación vestigial.

**Por qué importa la comparación en este TP:** el enunciado eligió segmentación pura *precisamente* para obligarnos a implementar y coordinar la compactación (la parte distribuida más difícil del TP) y a manejar el segfault por límite de segmento. Con paginación, ni la compactación ni el desalojo global de CPUs existirían — pero tampoco habríamos tenido que sincronizar tres módulos para mover memoria.

### 3.8 Memoria virtual, swapping y suspensión

El **swapping** de procesos completos es la forma más vieja de memoria virtual: cuando la RAM no alcanza, el SO elige un proceso (idealmente uno bloqueado hace rato), copia **toda su memoria a disco** y libera esos marcos/huecos. El proceso queda **suspendido** (los estados SUSP. * del punto 3.1). Para reanudarlo hay que traer todo de vuelta, lo que puede requerir esperar a que haya espacio.

Esto es más grueso que la paginación por demanda de los SO modernos (que mueven páginas individuales), pero conceptualmente es el mismo trade-off: usar el disco como extensión lenta de la RAM, sacrificando el tiempo de reanudación a cambio de multiprogramación.

Criterios de **des-suspensión** (a quién traer de vuelta primero cuando aparece memoria): tiene sentido priorizar por importancia (prioridad del proceso) y, a igualdad, por justicia (el que lleva más tiempo suspendido primero — evita inanición de suspendidos).

**En nuestro TP:**
- La **suspensión** la decide el KS (mediano plazo): si un proceso lleva más de `SUSPENSION_TIMEOUT` ms en BLOCK, pasa a SUSP. BLOCK y el KS le ordena a KM (`MSG_SUSPENDER_PROCESO`) mover sus segmentos al módulo **Swap**, bloque a bloque. La RAM liberada vuelve al pool de huecos.
- La **des-suspensión** se dispara cuando KM avisa `MSG_MAS_MEMORIA` (se conectó un Memory Stick nuevo, se liberó memoria, o se compactó). El KS recorre SUSP. READY **ordenando por prioridad y, a igual prioridad, por mayor tiempo suspendido**, y por cada candidato le pregunta a KM si sus segmentos entran *sin disparar compactación* (`MSG_DESSUSPENDER_PROCESO`). Si entran → READY; si no → el candidato y los que siguen vuelven a esperar. Exactamente el criterio teórico de arriba, que además es letra del enunciado.

---

## 4. Qué pedía el enunciado

Resumen de los requisitos del enunciado v1.1 para el Kernel Scheduler, con el estado y el lugar de la implementación:

| # | Requisito del enunciado | Implementación | Estado |
|---|---|---|---|
| 1 | Arranque `[config] [path proceso inicial]`; PID 0 con prioridad máxima | `main()` + `crear_proceso(argv[2], 0)` | ✅ |
| 2 | Conectarse a KM al iniciar; luego servidor multihilo para CPUs e IOs, con escucha siempre activa (hot-plug de CPUs) | handshake `MSG_KS_IDENTIFICACION` + accept-loop con un hilo `atender_cliente` por conexión | ✅ |
| 3 | Modelo de 7 estados | colas + `cambiar_estado()` con log obligatorio | ✅ |
| 4 | Largo plazo: NEW → READY sin restricción | inmediato en `crear_proceso()` | ✅ |
| 5 | BSOD: al aviso de corrupción de memoria, finalizar todo y salir | `manejar_bsod()` sobre `MSG_BSOD` | ✅ |
| 6 | Mediano plazo: BLOCK → SUSP. BLOCK por timeout configurable | `thread_suspension_timer` + `MSG_SUSPENDER_PROCESO` | ✅ |
| 7 | Des-suspensión al liberarse memoria / MS nuevo / compactación, por prioridad y antigüedad, sin disparar compactación | `manejar_mas_memoria()` sobre `MSG_MAS_MEMORIA` | ✅ |
| 8 | Corto plazo: FIFO, RR y CMN (no retroalimentadas), algoritmo por config | `thread_planificador` + `colas_ready[]` + `parsear_queues_algorithms` | ✅ |
| 9 | CMN: desalojo entre colas si `QUEUE_PREEMPTION=TRUE` | `verificar_preemption()` | ✅ |
| 10 | Syscalls de IO: EXEC → BLOCK y despachar otro proceso | handlers `MSG_SYSCALL_SLEEP/STDOUT/STDIN` | ✅ |
| 11 | Syscalls de memoria: al terminar, **volver a la misma CPU** | `manejar_mem_alloc/free` + `sacar_proc_de_exec_para_mem` | ✅ |
| 12 | Compactación: desalojar todas las CPUs, no despachar hasta confirmación de KM, desalojados al **frente** de READY | `handle_compactar()` + `encolar_al_frente_en_ready()` | ✅ |
| 13 | Mutexes con cola de espera FIFO | `ks_mutex.c` | ✅ |
| 14 | Herencia de prioridades ante inversión | `mutex_ks_lock/unlock` + elevación/restauración en `atender_cpu` | ✅ |
| 15 | STDOUT: pedir los bytes a KM y enviarlos a la IO | handler `MSG_SYSCALL_STDOUT` + `MSG_LEER_DATOS` | ✅ |
| 16 | STDIN: pedir lectura a la IO y escribir el resultado vía KM | handler `MSG_SYSCALL_STDIN` + `lista_stdin_pendientes` + `MSG_ESCRIBIR_DATOS` | ✅ |
| 17 | Todos los logs mínimos y obligatorios | ver sección 8 | ✅ |
| 18 | Config: `LOG_LEVEL`, `PLANIFICATION_ALGORITHM`, `QUEUES_ALGORITHMS`, `RR_QUANTUM`, `QUEUE_PREEMPTION`, `SUSPENSION_TIMEOUT` | `main()` lee todo | ✅ |

---

## 5. Implementación práctica

### 5.1 Estructura de archivos

```
kernel_scheduler/
├── makefile
├── kernel_scheduler.config.example
├── src/
│   ├── main.c        # ~1350 líneas: arranque, servidor, planificadores,
│   │                 #   handlers de CPU/IO/KM, compactación, BSOD
│   ├── proceso.h     # t_proceso y enum t_estado (los 7 estados)
│   ├── ks_mutex.c/h  # gestión de mutexes del lenguaje + herencia de prioridades
│   ├── ks_cmn.c/h    # parseo de QUEUES_ALGORITHMS para CMN
│   └── ks_compact.c/h# listener de KM testeable (versión inyectable para tests)
└── tests/
    ├── test_ks_mutex.c
    ├── test_ks_cmn.c
    └── test_ks_compact.c
```

La lógica "pura" (mutexes, parseo CMN, ruteo del listener) está separada de `main.c` justamente para poder testearla con cspecs sin sockets reales.

### 5.2 Estructuras de datos centrales

**El PCB simplificado** (`proceso.h`):

```c
typedef enum { NEW, READY, EXEC, BLOCK, SUSP_BLOCK, SUSP_READY, EXIT } t_estado;

typedef struct {
    int      PID;
    t_estado estado;
    uint32_t controladorDeProgramas;  // PC (el que manda es el de KM/CPU)
    int      prioridad;               // 0 = máxima; define la cola en CMN
    int      fd_cpu;                  // socket de la CPU que lo ejecuta (-1 si ninguna)
    int      preemptado;              // 1 = desalojado por preemption/compactación
                                      //     → vuelve al FRENTE de READY
    time_t   tiempo_suspension;       // epoch al pasar a SUSP_BLOCK (para el orden
                                      //     de des-suspensión)
} t_proceso;
```

Es deliberadamente mínimo: el contexto real (registros, PC efectivo, tabla de segmentos) vive en **KM**; el KS solo necesita lo que hace a la *planificación*. `fd_cpu` cumple el rol de "procesador asignado", `preemptado` distingue el tipo de desalojo al volver de la CPU, y `tiempo_suspension` implementa el criterio de antigüedad del enunciado.

**Las colas de estado** (todas `t_queue*` de commons, cada una con su propio `pthread_mutex_t`):

```c
t_queue *cola_new, *cola_exec;
t_queue *cola_block, *cola_susp_block, *cola_susp_ready, *cola_exit;
static t_queue* colas_ready[MAX_COLAS];   // N colas de READY (1 si FIFO/RR)
```

READY es un **arreglo de colas**: con FIFO o RR, `n_colas = 1`; con CMN hay una por nivel de prioridad. Esta unificación evita tener dos planificadores distintos.

**Los mutexes del lenguaje** (`ks_mutex.h`):

```c
typedef struct {
    char*           nombre;
    int             owner_pid;                 // -1 = libre
    int             owner_prioridad_original;  // para restaurar tras herencia
    t_queue*        cola_espera;               // t_mutex_waiter* en orden FIFO
    pthread_mutex_t lock;                      // lock interno por-mutex
} t_ks_mutex;

typedef struct { uint32_t pid; int prioridad; } t_mutex_waiter;
```

Detalle no obvio: cada waiter guarda **su** prioridad, porque cuando herede el mutex esa prioridad pasa a ser la nueva `owner_prioridad_original`.

**CPUs e IOs registradas:**

```c
typedef struct { int fd; int ocupada; } t_cpu_entry;   // en lista_cpus
static int fd_io_sleep, fd_io_stdout, fd_io_stdin;     // una IO por tipo
```

**Pedidos de STDIN en vuelo:** al despachar un STDIN a la IO hay que recordar *dónde* escribir lo que el usuario tipee; `lista_stdin_pendientes` guarda `{pid, dir_logica, tamanio}` hasta que llega `MSG_IO_STDIN_DATOS`.

### 5.3 Modelo de concurrencia

El KS es fuertemente multihilo. Inventario completo de hilos:

| Hilo | Cantidad | Qué hace |
|---|---|---|
| `main` (accept-loop) | 1 | Acepta conexiones entrantes y lanza un `atender_cliente` por cada una |
| `atender_cliente` → `atender_cpu` | 1 por CPU | Loop de recepción de la CPU: syscalls y devoluciones de proceso |
| `atender_cliente` → `atender_io` | 1 por IO | Loop de recepción de la IO: `MSG_IO_FIN` y `MSG_IO_STDIN_DATOS` |
| `thread_km_listener` | 1 | **Único** lector del socket con KM; rutea respuestas y despacha eventos asincrónicos |
| `thread_planificador` | 1 | Corto plazo: espera (READY ∧ CPU libre) y despacha |
| `thread_largo_plazo` | 1 | Reintenta des-suspensiones cuando se lo señala (`sem_largo_plazo`) |
| `thread_quantum_timer` | 1 por despacho RR | Duerme el quantum y, si el proceso sigue en EXEC, interrumpe su CPU |
| `thread_suspension_timer` | 1 por entrada a BLOCK | Duerme `SUSPENSION_TIMEOUT` y, si el proceso sigue en BLOCK, lo suspende |
| `handle_compactar` | efímero | Coreografía de compactación (lanzado por el listener para no bloquearse) |
| `manejar_mas_memoria` | efímero | Des-suspensión masiva (lanzado por el listener, ídem) |
| `manejar_mem_alloc` / `manejar_mem_free` | efímero | Syscalls de memoria (lanzado por `atender_cpu`, ídem — ver sección 6) |

Primitivas de sincronización principales:

| Primitiva | Protege / señala |
|---|---|
| un `pthread_mutex_t` **por cola** de estado | acceso concurrente a cada cola |
| `mutex_cpus`, `mutex_io`, `mutex_pid`, `mutex_stdin_pendientes` | lista de CPUs, fds de IO, contador de PIDs, pendientes de STDIN |
| `sem_cpu_disponible` | contador de eventos "hay trabajo para el planificador" (proceso nuevo en READY **o** CPU que se liberó) |
| `sem_largo_plazo` | despierta a `thread_largo_plazo` para reintentar des-suspensiones |
| `sem_planificador_ok` | semáforo binario: 0 = compactación en curso → el planificador no despacha |
| `sem_cpus_devueltas` | cada CPU desalojada durante la compactación lo señala; `handle_compactar` espera N |
| `mutex_km_req` + `mutex_km_resp` + `cond_km_resp` | el patrón request/response con KM (5.13) |

Regla de diseño que atraviesa todo el módulo: **ningún hilo que atiende un socket puede quedarse esperando una respuesta que llega por *otro* socket que él mismo no lee**. Cada vez que violamos esa regla tuvimos un deadlock (sección 6).

### 5.4 Protocolo de comunicación

Todos los mensajes usan el framing común de `utils` (`t_mensaje = {op_code, payload_size, payload}` con los enteros en *network byte order*). Mensajes en los que participa el KS:

**CPU → KS:**

| Mensaje | Payload | Significado |
|---|---|---|
| `MSG_CPU_IDENTIFICACION` | id string | handshake inicial |
| `MSG_DEVOLVER_PROCESO` | `{pid, motivo, pc}` | la CPU devuelve el proceso (syscall no bloqueante / EXIT / error / interrupción) |
| `MSG_SYSCALL_SLEEP` | `{pid, tiempo_ms}` | syscall SLEEP |
| `MSG_SYSCALL_STDOUT` / `MSG_SYSCALL_STDIN` | `{pid, dir_logica, tamanio}` | syscalls de IO con memoria |
| `MSG_SYSCALL_EXIT` | `{pid}` | fin del proceso |
| `MSG_MUTEX_CREATE/LOCK/UNLOCK` | `{pid, nombre[]}` | syscalls de mutex |
| `MSG_MEM_ALLOC` | `{pid, id_segmento, tamanio}` | crear segmento |
| `MSG_MEM_FREE` | `{pid, id_segmento}` | eliminar segmento |
| `MSG_INIT_PROC` | `{pid_padre, path[], prioridad}` | crear proceso hijo |

**KS → CPU:**

| Mensaje | Payload | Significado |
|---|---|---|
| `MSG_DESPACHAR_PROCESO` | `{pid}` | ejecutá este proceso |
| `MSG_INTERRUPCION_CPU` | `{pid, motivo}` | desalojá (motivo: QUANTUM o DESALOJO) |
| `MSG_OK` / `MSG_ERROR` | — | resultado de MEM_ALLOC/MEM_FREE/EXIT/INIT_PROC |

**KS ↔ KM:**

| Mensaje | Dirección | Significado |
|---|---|---|
| `MSG_CREAR_PROCESO` | KS → KM | nuevo PID + path del script |
| `MSG_CREAR_SEGMENTO` / `MSG_ELIMINAR_SEGMENTO` | KS → KM | por MEM_ALLOC / MEM_FREE |
| `MSG_SUSPENDER_PROCESO` / `MSG_DESSUSPENDER_PROCESO` | KS → KM | mediano plazo |
| `MSG_LEER_DATOS` → `MSG_LEER_DATOS_RESP` | KS → KM → KS | bytes para STDOUT |
| `MSG_ESCRIBIR_DATOS` | KS → KM | bytes leídos por STDIN |
| `MSG_COMPACTAR` | KM → KS | "desalojá todas las CPUs, necesito compactar" |
| `MSG_FIN_COMPACTACION` | KS → KM | "CPUs quietas, compactá" (KM responde `MSG_OK` al terminar) |
| `MSG_MAS_MEMORIA` | KM → KS | hay memoria nueva/liberada → intentá des-suspender |
| `MSG_BSOD` | KM → KS | memoria corrupta (MS desconectado) → matar todo |

**KS ↔ IO:**

| Mensaje | Dirección | Significado |
|---|---|---|
| `MSG_IO_IDENTIFICACION` | IO → KS | tipo (SLEEP/STDIN/STDOUT) |
| `MSG_IO_SLEEP` | KS → IO | `{pid, tiempo_ms}` |
| `MSG_IO_STDOUT` | KS → IO | `{pid, bytes[]}` |
| `MSG_IO_STDIN` | KS → IO | `{pid, n_bytes}` |
| `MSG_IO_FIN` | IO → KS | `{pid}` — la operación terminó |
| `MSG_IO_STDIN_DATOS` | IO → KS | `{pid, n_bytes, datos[]}` |

### 5.5 Arranque del módulo

Secuencia de `main()`:

1. Validar argumentos y leer el config (algoritmo, quantum, timeout de suspensión; si el algoritmo es `CMN`, además `QUEUES_ALGORITHMS` y `QUEUE_PREEMPTION`).
2. Si es FIFO o RR: `n_colas = 1` y ese algoritmo va en la cola 0 — la vía degenerada de CMN.
3. **Conectarse sincrónicamente a KM** (handshake `MSG_KS_IDENTIFICACION` → esperar `MSG_OK`). Si KM no está, el KS no arranca — es el orden de dependencias del enunciado. Log obligatorio `## Conectado a Kernel Memory`.
4. Crear todas las colas, mutexes y semáforos; `mutexes_init()` para la lista de mutexes del lenguaje.
5. Crear el servidor TCP en `KERNEL_SCHEDULER_PORT`.
6. Lanzar los tres hilos permanentes: `thread_km_listener` (desde este punto es el **único** lector del socket con KM), `thread_planificador` y `thread_largo_plazo`.
7. **Crear el PID 0** con prioridad 0 a partir de `argv[2]`.
8. Entrar al accept-loop infinito: cada conexión entrante se atiende en su propio hilo detached (`atender_cliente`), que según el primer mensaje de identificación se convierte en el loop de una CPU o de una IO. Esto cumple el requisito de "escucha siempre activa" y permite el hot-plug de CPUs.

### 5.6 Planificación de largo plazo

**Admisión (NEW → READY).** `crear_proceso(path, prioridad)`:

1. Toma un PID nuevo de un contador global protegido por mutex.
2. Crea el `t_proceso` en NEW (log obligatorio de creación).
3. Le pide a KM crear el proceso (`MSG_CREAR_PROCESO {pid, path}`) — KM valida el script y crea el contexto vacío. Si KM rechaza, el proceso no nace.
4. Transición inmediata a READY y `encolar_en_ready()`.

No hay restricción de admisión porque los procesos **nacen sin memoria de datos**: los segmentos aparecen recién cuando el programa ejecuta `MEM_ALLOC`. Es la letra exacta del enunciado.

Los procesos posteriores al PID 0 nacen por la syscall `INIT_PROC {path, prioridad}`, que llega desde una CPU y llama al mismo `crear_proceso`. El proceso padre **no se bloquea**: se responde `MSG_OK` y sigue ejecutando.

**El hilo de largo plazo.** `thread_largo_plazo` duerme en `sem_largo_plazo` y cada vez que lo señalan corre `manejar_mas_memoria()` (ver 5.8). ¿Quién lo señala? Los caminos donde un proceso entra a SUSP. READY (fin de IO tardío, mutex liberado a un waiter suspendido): en esos casos hay que *intentar* traerlo a RAM aunque KM no haya anunciado memoria nueva.

### 5.7 Planificación de corto plazo

**El dispatcher.** `thread_planificador` es un loop eterno sobre un patrón productor/consumidor:

```c
while (1) {
    sem_wait(&sem_cpu_disponible);       // esperar "hay trabajo"
    sem_wait(&sem_planificador_ok);      // barrera de compactación:
    sem_post(&sem_planificador_ok);      //   si vale 0, acá se frena

    // 1) elegir proceso: primera cola de READY no vacía, de mayor a menor prioridad
    for (i = 0; i < n_colas && !proc; i++)
        proc = queue_pop(colas_ready[i]);          // (con su mutex)
    if (!proc) continue;

    // 2) elegir CPU libre
    cpu = primera cpu con ocupada == 0;            // (con mutex_cpus)
    if (!cpu) { devolver proc al final de su cola; continue; }

    // 3) despachar
    despachar(proc, cpu);                          // EXEC + push a cola_exec
                                                   // + MSG_DESPACHAR_PROCESO
    // 4) si la cola elegida es RR, armar el timer de quantum
    if (algoritmos_cola[nivel] == "RR")
        lanzar thread_quantum_timer(proc->PID);
}
```

`sem_cpu_disponible` se postea en cada evento que puede habilitar un despacho: proceso que entra a READY, CPU que se conecta, CPU que se libera (proceso bloqueado/terminado/desalojado), fin de compactación. Puede haber posts "de más" (el planificador se despierta y no encuentra nada); es inofensivo — vuelve a dormir.

La selección por prioridad es la definición de CMN: **siempre** la cola no vacía de índice más bajo. Con `n_colas == 1` esto degenera exactamente en FIFO o RR globales.

**Round Robin.** No hay un reloj central: cada despacho a una cola RR lanza un `thread_quantum_timer` con el PID. El hilo duerme `RR_QUANTUM` ms con `nanosleep` y al despertar verifica **si ese PID sigue en `cola_exec`**:

- Si sigue → le envía a su CPU `MSG_INTERRUPCION_CPU {pid, MOTIVO_INTERRUPCION_QUANTUM}`. La CPU termina la instrucción en curso, guarda el contexto en KM y devuelve el proceso con motivo INTERRUPCION. El KS lo pasa a READY **al final** de su cola (log obligatorio de fin de quantum).
- Si ya no está (se bloqueó, terminó o fue desalojado antes) → el timer muere sin hacer nada.

Esta verificación post-sueño es la que evita el clásico bug de "interrupción tardía": sin ella, el timer de un proceso que ya se bloqueó podría desalojar al proceso *siguiente* en esa CPU. Verificar por PID en `cola_exec` (y no solo por CPU) cierra esa ventana.

**Desalojo entre colas (CMN + `QUEUE_PREEMPTION=TRUE`).** Cada vez que un proceso entra a READY, `verificar_preemption(entrante)` recorre `cola_exec` buscando un proceso ejecutando con prioridad **numéricamente mayor** (= menos prioritario). Si lo encuentra:

1. Lo marca `preemptado = 1`.
2. Loguea el log obligatorio de desalojo por cola más prioritaria.
3. Le envía `MSG_INTERRUPCION_CPU {pid, MOTIVO_INTERRUPCION_DESALOJO}`.

Cuando esa CPU devuelva el proceso (motivo INTERRUPCION), el flag `preemptado` hace que se lo encole **al frente** de su cola de READY (`encolar_al_frente_en_ready`) en lugar de al final: el proceso no pierde su turno, solo cede la CPU. El entrante más prioritario será elegido por el planificador porque su cola se revisa primero.

**Devolución de procesos.** Todo proceso vuelve de la CPU por `MSG_DEVOLVER_PROCESO {pid, motivo, pc}`; el handler en `atender_cpu` lo saca de `cola_exec` (marcando la CPU libre) y decide según el motivo:

| Motivo | Acción |
|---|---|
| `EXIT` | → EXIT, log de fin, liberar CPU |
| `ERROR` (segfault detectado por la MMU) | → EXIT con motivo `SEG_FAULT` en el log |
| `INTERRUPCION` con `preemptado=1` | → READY **al frente** (preemption/compactación); si hay compactación en curso, señala `sem_cpus_devueltas` |
| `INTERRUPCION` con `preemptado=0` | → READY al final (fin de quantum) |
| `SYSCALL` | → READY al final (syscall no bloqueante ya atendida: MUTEX_CREATE, MUTEX_UNLOCK, MUTEX_LOCK con mutex libre) |

### 5.8 Planificación de mediano plazo

**Suspensión (BLOCK → SUSP. BLOCK).** Cada vez que un proceso entra a BLOCK (`mover_a_block`), además de encolarlo y liberar la CPU, se lanza un `thread_suspension_timer` con su PID:

```c
nanosleep(SUSPENSION_TIMEOUT);
proc = sacar_de_block(pid);          // atómico: si no está, alguien lo sacó antes
if (!proc) return;                   //   (terminó su IO a tiempo) → no hacer nada

proc->tiempo_suspension = time(NULL);
cambiar_estado(proc, SUSP_BLOCK);    // + push a cola_susp_block
km_request(MSG_SUSPENDER_PROCESO, pid);   // KM mueve sus segmentos a Swap
```

Puntos finos:

- La verificación "¿sigue en BLOCK?" es **atómica con la extracción**: `sacar_de_block` busca y saca bajo el mutex de la cola. Así no hay carrera con el fin de IO que llega en el mismo instante — uno de los dos hilos se lleva el proceso y el otro no encuentra nada.
- La suspensión aplica a **cualquier** causa de BLOCK: IO en curso **y también** espera de mutex. Un proceso que espera un mutex mucho tiempo termina suspendido, y el enunciado lo contempla: por eso `MUTEX_UNLOCK` tiene que buscar al waiter también en SUSP. BLOCK (ver 5.10).
- Recién *después* de mover el proceso de cola se le avisa a KM. KM copia los segmentos del proceso al Swap bloque a bloque y libera los huecos; esa liberación puede a su vez disparar un `MSG_MAS_MEMORIA` de vuelta (más memoria libre → quizá algún suspendido ya entra).

**Fin de la espera estando suspendido (SUSP. BLOCK → SUSP. READY).** Cuando la IO de un proceso suspendido termina (o le ceden el mutex que esperaba), el proceso no puede ir a READY directo: **su memoria está en disco**. Pasa a SUSP. READY y se señala `sem_largo_plazo` para intentar traerlo.

**Des-suspensión (SUSP. READY → READY).** `manejar_mas_memoria()` — disparado por `MSG_MAS_MEMORIA` de KM (stick nuevo, memoria liberada, compactación) o por `thread_largo_plazo`:

```c
1. Vaciar cola_susp_ready a una lista local (bajo su mutex).
2. Ordenar por (prioridad, tiempo_suspension):
     mayor prioridad primero; a igual prioridad, el que lleva más tiempo suspendido.
3. Para cada candidato en orden:
     resp = km_request(MSG_DESSUSPENDER_PROCESO, pid);
     si MSG_OK  → KM recreó sus segmentos en RAM (sin compactar, restricción
                  del enunciado) → READY + encolar_en_ready()
     si ERROR   → no hay lugar ni para este: devolver este y TODOS los restantes
                  a cola_susp_ready y cortar.
```

El corte temprano es correcto porque la lista está ordenada: si no entró el más prioritario/más viejo, no corresponde saltearlo y meter a uno menos prioritario (sería inanición del importante). El costo es que un proceso chico podría haber entrado — es el trade-off justicia vs. aprovechamiento, y el enunciado pide justicia.

### 5.9 Atención de syscalls

Todas las syscalls llegan por el socket de la CPU y se atienden en su hilo `atender_cpu`. Se dividen en tres familias con semánticas distintas:

#### Syscalls bloqueantes de IO (SLEEP, STDOUT, STDIN)

Patrón común: log obligatorio de syscall → `sacar_de_exec(pid)` (libera la CPU) → `mover_a_block(proc)` (encola en BLOCK, postea `sem_cpu_disponible` para que otro proceso tome la CPU, arma el timer de suspensión) → interactuar con la IO. La CPU, por su parte, ya guardó el contexto en KM y quedó esperando otro proceso: **no** espera respuesta del KS.

- **SLEEP `{pid, tiempo_ms}`**: se reenvía tal cual a la IO SLEEP (`MSG_IO_SLEEP`). La IO duerme y devuelve `MSG_IO_FIN {pid}`.

- **STDOUT `{pid, dir_logica, tamanio}`**: el KS es **intermediario entre la memoria y el dispositivo**:
  1. `km_request(MSG_LEER_DATOS {pid, dir_logica, tamanio})` — KM traduce la dirección con la tabla de segmentos del proceso y junta los bytes (que pueden estar repartidos entre varios Memory Sticks).
  2. Con la respuesta (`MSG_LEER_DATOS_RESP`), arma `[pid | bytes...]` y lo envía a la IO STDOUT (`MSG_IO_STDOUT`).
  3. La IO imprime y responde `MSG_IO_FIN`.

- **STDIN `{pid, dir_logica, tamanio}`**: el flujo inverso, en dos tiempos:
  1. Se guarda `{pid, dir_logica, tamanio}` en `lista_stdin_pendientes` — la IO no necesita saber la dirección, pero el KS la va a necesitar cuando vuelvan los datos.
  2. Se envía `MSG_IO_STDIN {pid, n_bytes}` a la IO, que le pide al usuario tipear.
  3. Al llegar `MSG_IO_STDIN_DATOS {pid, n_bytes, datos}` (lo recibe `atender_io`), se recupera la dirección pendiente, se arma `[pid | dir_logica | tamanio | datos]` y se escribe en la memoria del proceso con `km_request(MSG_ESCRIBIR_DATOS)`.

**Fin de IO — el cruce con el mediano plazo.** Al llegar `MSG_IO_FIN` (o los datos de STDIN), `atender_io` busca el proceso primero en BLOCK y, si no está, en SUSP. BLOCK:

- Estaba en BLOCK (la IO terminó *antes* del timeout) → READY, log `finalizó IO y pasa a READY`.
- Estaba en SUSP. BLOCK (terminó *después*) → SUSP. READY, log correspondiente, y `sem_post(&sem_largo_plazo)` para que el largo plazo intente des-suspenderlo.

*Nota de diseño (issue #64):* el enunciado dice que la CPU envía la dirección **física** para STDIN/STDOUT; en nuestra implementación viaja la dirección **lógica** y KM hace la traducción. Lo decidimos así porque KM ya tiene las tablas de segmentos y la lógica multi-stick, y porque una dirección física cacheada por la CPU se invalidaría si una compactación ocurre mientras el proceso está bloqueado — con la dirección lógica el problema desaparece.

#### Syscalls de memoria (MEM_ALLOC, MEM_FREE) — redespacho a la misma CPU

Estas syscalls tienen la particularidad (letra del enunciado) de que al terminar **el proceso vuelve a la misma CPU** que hizo la llamada — no pasa por READY ni se replanifica. La CPU envía la syscall y se queda esperando `MSG_OK`/`MSG_ERROR` en su socket.

La implementación tiene dos piezas específicas:

- `sacar_proc_de_exec_para_mem(pid)`: saca el proceso de `cola_exec` **sin marcar la CPU como libre** (`ocupada` queda en 1, `fd_cpu` se conserva). El planificador no puede despacharle otro proceso a esa CPU mientras la syscall está en vuelo.
- El trabajo pesado va en un **hilo efímero** (`manejar_mem_alloc` / `manejar_mem_free`): hace `km_request(MSG_CREAR_SEGMENTO | MSG_ELIMINAR_SEGMENTO)`, reinserta el proceso en `cola_exec` con `re_exec_sin_despachar()` (sin `MSG_DESPACHAR_PROCESO` — la CPU nunca soltó el proceso) y responde `MSG_OK`/`MSG_ERROR` directamente al `fd_cpu`. La CPU retoma el ciclo de instrucción donde estaba.

¿Por qué el hilo aparte y no atenderlo inline en `atender_cpu`? Porque `km_request` **bloquea** hasta que KM responda, y la creación de un segmento puede tardar arbitrariamente: si necesita compactación, KM primero le pide al KS que desaloje todas las CPUs. Ese desalojo requiere que los hilos `atender_cpu` estén **libres para recibir** las devoluciones de las CPUs. Si `atender_cpu` estuviera clavado dentro de `km_request`, la CPU desalojada no podría devolver su proceso → la compactación nunca juntaría las N confirmaciones → KM nunca respondería el `MSG_CREAR_SEGMENTO` → **deadlock circular entre tres módulos**. Con el hilo efímero, `atender_cpu` vuelve inmediatamente a su `recibir_mensaje` y el ciclo se cierra. (Este fue el issue #67; más en la sección 6.)

El orden `re_exec_sin_despachar` **antes** de responder a la CPU tampoco es casual: si la CPU recibiera el `MSG_OK`, ejecutara rapidísimo la siguiente instrucción y devolviera el proceso antes de que el KS lo reinsertara en `cola_exec`, `sacar_de_exec` no lo encontraría y el proceso se perdería.

#### Syscalls de control (INIT_PROC, EXIT) y de mutex

- **INIT_PROC `{path, prioridad}`**: crea el proceso hijo con `crear_proceso` (mismo camino que el PID 0) y responde `MSG_OK`. El padre sigue en EXEC.
- **EXIT `{pid}`**: proceso a EXIT, log de fin, CPU liberada, `MSG_OK` a la CPU (que queda ociosa esperando otro despacho).
- **MUTEX_***: sección siguiente.

### 5.10 Mutex y herencia de prioridades

La gestión vive en `ks_mutex.c` (lógica pura, testeable) y los handlers de `atender_cpu` (efectos sobre colas y prioridades). Para las tres syscalls la CPU **devuelve el proceso al KS antes** (con motivo SYSCALL) y no espera respuesta directa; el destino del proceso lo decide el KS.

**MUTEX_CREATE `{nombre}`** — no bloqueante. Crea el mutex (libre, sin owner) si no existía; si existía, no pasa nada. El proceso vuelve a READY por el camino normal de `MSG_DEVOLVER_PROCESO`.

**MUTEX_LOCK `{nombre}`** — `mutex_ks_lock(pid, prioridad, nombre, ...)` distingue tres resultados:

| Resultado | Situación | Acción del KS |
|---|---|---|
| `0` | mutex libre | el proceso lo toma (log `Toma el Mutex`), guarda `owner_prioridad_original`, y vuelve a **READY** — sigue ejecutando cuando lo replanifiquen |
| `1` | mutex tomado | el proceso se encola como waiter (FIFO, con su prioridad) y el KS lo mueve a **BLOCK**; si además el waiter es más prioritario que el owner → **herencia** |
| `-1` | mutex inexistente | no debería pasar en scripts bien formados |

**La herencia en detalle.** `mutex_ks_lock` compara la prioridad del waiter con la **prioridad original** del owner (no la actual — así el owner nunca "hereda de un heredero" hacia abajo, y varias herencias sucesivas siempre comparan contra la misma base). Si el waiter es más prioritario, devuelve por parámetros de salida `owner_a_elevar` y `nueva_prioridad_owner`, y el handler:

1. Busca al owner en EXEC o en las colas de READY (`buscar_proceso_activo`).
2. Si su prioridad actual es peor que la nueva, la eleva y loguea el log obligatorio `## <PID> Cambio de prioridad: <vieja> - <nueva>`.

El efecto práctico bajo CMN: el owner elevado entra, en sus próximos pasos por READY, a la cola del waiter — deja de ser pisoteado por los procesos de prioridad media. Exactamente la solución del caso Mars Pathfinder de la sección 3.6.

**MUTEX_UNLOCK `{nombre}`** — `mutex_ks_unlock`:

1. Verifica que quien libera sea el owner (si no, `-1` y no pasa nada).
2. Log obligatorio `Libera el Mutex`. Devuelve `prioridad_restaurar` = la prioridad original del owner: si había sido elevada por herencia, el handler la **restaura** (con su log de cambio de prioridad). *El préstamo de prioridad dura exactamente lo que dura la posesión del mutex.*
3. Si hay waiters: desencola el primero (FIFO — el orden de llegada, como pide el enunciado), lo hace **nuevo owner** (log `Toma el Mutex`, su prioridad pasa a ser la nueva original) y devuelve su PID.
4. El handler despierta al waiter: lo busca en **BLOCK → READY**; si no está ahí, en **SUSP. BLOCK → SUSP. READY** (+ señal al largo plazo). Este segundo camino existe porque un waiter puede haber sido suspendido por timeout mientras esperaba el mutex (5.8).

Detalle de concurrencia interna: cada `t_ks_mutex` tiene su propio `pthread_mutex_t` y la lista global tiene otro; las operaciones toman la lista solo para *encontrar* el mutex y en seguida la sueltan quedándose con el lock fino. Dos procesos operando sobre mutexes distintos no se serializan entre sí.

### 5.11 Desalojo por compactación

La compactación es la operación más coreografiada del TP porque involucra a KM, al KS y a **todas** las CPUs, y porque mover segmentos con CPUs ejecutando corrompería memoria (una CPU traduciendo con una base vieja leería datos de otro proceso).

**Disparador.** Un `MEM_ALLOC` llega a KM; hay espacio libre total suficiente pero no contiguo (fragmentación externa, 3.7). KM envía `MSG_COMPACTAR` al KS y se queda esperando.

**La coreografía completa:**

```
CPU_x ── MEM_ALLOC ──► KS ── MSG_CREAR_SEGMENTO ──► KM
                                                     │ no hay hueco contiguo
KS ◄──────────────── MSG_COMPACTAR ─────────────────┘
│
│ (thread_km_listener lo despacha a un hilo handle_compactar
│  para seguir libre — ver por qué en la sección 6)
│
├─ 1. log "## Inicio de compactación"
├─ 2. sem_wait(sem_planificador_ok)      ← el planificador NO despacha más
├─ 3. para cada proceso en EXEC:
│        marcar preemptado = 1
│        MSG_INTERRUPCION_CPU {pid, DESALOJO} a su CPU
├─ 4. esperar N posts de sem_cpus_devueltas
│        (cada CPU guarda contexto en KM y devuelve su proceso;
│         el handler lo pone al FRENTE de READY y postea el semáforo)
├─ 5. km_request(MSG_FIN_COMPACTACION)   ← "todas quietas, compactá"
│        KM mueve los segmentos, actualiza TODAS las bases en las
│        tablas de segmentos (con COMPACTION_DELAY), reintenta el
│        MEM_ALLOC original, y responde MSG_OK
├─ 6. log "## Fin de compactación"
└─ 7. sem_post(sem_planificador_ok) + sem_post(sem_cpu_disponible)
         → se replanifica todo según el algoritmo vigente
```

Decisiones clave:

- **Frenar el planificador primero** (paso 2): si se desalojaran las CPUs pero el planificador siguiera vivo, despacharía inmediatamente los procesos recién devueltos a las CPUs recién liberadas, y la memoria nunca quedaría quieta. `sem_planificador_ok` como semáforo binario implementa una "compuerta" limpia: el patrón `wait+post` en el loop del planificador lo deja pasar cuando vale 1 y lo frena cuando vale 0.
- **Los desalojados van al frente de READY** (requisito explícito del enunciado): ellos no hicieron nada para perder la CPU; al terminar la compactación deben ser los primeros en volver. Se reutiliza el flag `preemptado` y `encolar_al_frente_en_ready`.
- **Contar devoluciones, no interrupciones** (paso 4): el desalojo no es instantáneo — la CPU termina su instrucción en curso y guarda contexto. `sem_cpus_devueltas` se postea desde el handler de devolución solo si `compactando == 1`, y `handle_compactar` espera exactamente N posts, con N capturado al momento de interrumpir.
- Mientras tanto, la **CPU que pidió el MEM_ALLOC** sigue esperando su `MSG_OK`: su hilo `manejar_mem_alloc` está bloqueado dentro de `km_request(MSG_CREAR_SEGMENTO)`, y KM le va a responder recién después de compactar y reintentar. Todo cierra porque ese hilo es efímero y no bloquea a nadie más.

### 5.12 BSOD

Si un Memory Stick **con datos** se desconecta, la memoria global queda corrupta: hay segmentos cuyo contenido ya no existe. KM detecta la desconexión y envía `MSG_BSOD`. No hay recuperación posible — es el "Act of Faith" del título del TP.

`manejar_bsod()`:

1. Log de advertencia de BSOD.
2. Interrumpe a todas las CPUs en EXEC (`MOTIVO_INTERRUPCION_DESALOJO`) — no van a poder devolver el proceso a tiempo, pero al menos dejan de ejecutar sobre memoria corrupta.
3. Vacía **todas** las colas (EXEC, cada READY, BLOCK, SUSP. BLOCK, SUSP. READY) finalizando cada proceso con el log `## (<PID>) finalizó su ejecución con motivo de BSOD`.
4. `exit(EXIT_FAILURE)` — el módulo entero muere, como pide el enunciado.

### 5.13 El patrón `km_request` / `thread_km_listener`

Es la pieza de infraestructura más importante del módulo, porque **muchos hilos** necesitan hablar con KM sobre **un único socket**, y además KM manda mensajes *no solicitados* (`MSG_COMPACTAR`, `MSG_MAS_MEMORIA`, `MSG_BSOD`) mezclados con las respuestas.

**El problema:** si cada hilo hiciera `enviar_mensaje` + `recibir_mensaje` sobre `fd_km`:
- dos requests concurrentes intercalarían sus respuestas (el hilo A podría leer la respuesta del hilo B), y
- un hilo esperando su respuesta podría "comerse" un `MSG_COMPACTAR` asincrónico.

**La solución — un único lector + request/response con condvar:**

```c
// Lado del que pide (cualquier hilo):
static t_mensaje* km_request(uint32_t op, void* payload, uint32_t size) {
    pthread_mutex_lock(&mutex_km_req);        // 1 request en vuelo a la vez
    enviar_mensaje(fd_km, op, payload, size);
    pthread_mutex_lock(&mutex_km_resp);
    while (ultimo_resp_km == NULL)            // esperar que el listener la deje
        pthread_cond_wait(&cond_km_resp, &mutex_km_resp);
    resp = ultimo_resp_km;  ultimo_resp_km = NULL;
    pthread_mutex_unlock(&mutex_km_resp);
    pthread_mutex_unlock(&mutex_km_req);
    return resp;
}

// Lado del único lector (thread_km_listener):
switch (msg->op_code) {
    case MSG_OK: case MSG_ERROR:
    case MSG_TABLA_SEGMENTOS: case MSG_LEER_DATOS_RESP:
        // es LA respuesta al request en vuelo → entregarla por la condvar
        ultimo_resp_km = msg;  pthread_cond_signal(&cond_km_resp);  break;
    case MSG_COMPACTAR:    lanzar hilo handle_compactar;    break;  // asincrónico
    case MSG_MAS_MEMORIA:  lanzar hilo manejar_mas_memoria; break;  // asincrónico
    case MSG_BSOD:         manejar_bsod();                  break;  // terminal
}
```

Propiedades:

- `mutex_km_req` garantiza **a lo sumo un request pendiente**: la respuesta que llegue es inequívocamente para el que la espera. Simple y suficiente (no hicieron falta IDs de correlación).
- El listener **clasifica por op_code**: los cuatro op_codes de respuesta van a la condvar; los eventos van a hilos nuevos. Que vayan a *hilos nuevos* y no se atiendan inline es crítico: `handle_compactar` y `manejar_mas_memoria` llaman a `km_request`, cuya respuesta la lee... el propio listener. Si el listener los ejecutara inline se esperaría a sí mismo (deadlock #1 de la sección 6).
- El costo aceptado: los requests a KM se **serializan**. Con la carga de este TP no fue un cuello de botella, y a cambio el protocolo es imposible de desincronizar.

`ks_compact.c` contiene una versión parametrizable de este listener (`km_listener_run` con contexto inyectable) usada por los tests unitarios para validar el ruteo sin sockets reales.

---

## 6. Problemas encontrados y soluciones

Crónica de los bugs reales del módulo, reconstruida de los commits, los issues y los informes internos (`Doc/informes_temporales/`). Los tres primeros son variantes del mismo patrón de deadlock distribuido y son la mejor lección del TP.

### 6.1 Deadlock: el listener de KM se esperaba a sí mismo

**Síntoma:** al disparar la primera compactación, el KS se congelaba entero.

**Causa:** la primera versión atendía `MSG_COMPACTAR` *inline* dentro de `thread_km_listener`. Pero `handle_compactar` termina llamando `km_request(MSG_FIN_COMPACTACION)`, que espera una respuesta de KM... que solo puede entregar el propio `thread_km_listener`, que estaba ocupado ejecutando `handle_compactar`. Espera circular de un hilo consigo mismo: deadlock instantáneo.

**Fix (commit `89f4ba1`):** el listener despacha `MSG_COMPACTAR` a un hilo detached y vuelve inmediatamente a `recibir_mensaje`. El comentario quedó en el código como advertencia.

**Reincidencia (commit `66d4752`):** exactamente el mismo patrón con `MSG_MAS_MEMORIA` → `manejar_mas_memoria` → `km_request(MSG_DESSUSPENDER_PROCESO)`. Mismo fix: hilo aparte. Lección aprendida y generalizada: **el hilo lector de un socket jamás ejecuta inline nada que pueda escribir-y-esperar sobre ese mismo socket.**

### 6.2 Deadlock a tres módulos: MEM_ALLOC + compactación (issue #67)

**Síntoma:** un `MEM_ALLOC` que necesitaba compactación colgaba al sistema completo (KS, KM y todas las CPUs).

**Causa:** la cadena circular más larga del TP:

```
atender_cpu (hilo de CPU_x) ── bloqueado en km_request(MSG_CREAR_SEGMENTO)
   ▲                                                        │
   │ CPU_x desalojada no puede                              ▼
   │ entregar MSG_DEVOLVER_PROCESO                KM espera MSG_FIN_COMPACTACION
   │ (su hilo lector en KS está ocupado)                    │
   │                                                        ▼
   └──────── handle_compactar espera sem_cpus_devueltas ◄───┘
```

El hilo `atender_cpu` que recibió el `MEM_ALLOC` quedaba bloqueado dentro de `km_request`; cuando KM pedía compactar y el KS interrumpía a esa misma CPU, la devolución del proceso llegaba a un hilo que no estaba escuchando. Nadie avanzaba.

**Fix (commit `8d80525`):** las syscalls de memoria se atienden en hilos efímeros (`manejar_mem_alloc/free`) y `atender_cpu` vuelve al `recibir_mensaje` de inmediato. Además hubo que inventar `sacar_proc_de_exec_para_mem` / `re_exec_sin_despachar` para que la CPU quedara reservada (ocupada, sin ser re-despachada) mientras la syscall estaba en vuelo — el enunciado exige que el proceso vuelva a **la misma CPU**.

### 6.3 Deadlock: STDOUT esperaba una respuesta que se descartaba

**Síntoma:** la primera syscall STDOUT de cualquier proceso dejaba colgado el hilo de esa CPU para siempre.

**Causa:** `thread_km_listener` ruteaba a la condvar solo `MSG_OK`, `MSG_ERROR` y `MSG_TABLA_SEGMENTOS`. La respuesta de KM a `MSG_LEER_DATOS` es `MSG_LEER_DATOS_RESP`, que caía en el `default` del switch y se **descartaba con un warning**. `km_request(MSG_LEER_DATOS)` esperaba eternamente una respuesta que ya había sido liberada.

**Fix (commit `d1ed490`):** agregar `case MSG_LEER_DATOS_RESP:` al ruteo. Un fix de una línea que dejó una lección sobre el patrón "único lector": el clasificador de op_codes es un punto único de falla — **todo op_code de respuesta nuevo tiene que agregarse al switch**, y el `default` con warning (en vez de descarte silencioso) fue lo que permitió diagnosticarlo con el log.

### 6.4 MUTEX_LOCK bloqueante no liberaba la CPU (commit `892d745`)

**Síntoma:** cuando un proceso quedaba bloqueado esperando un mutex, su CPU quedaba "ocupada" para siempre y el planificador nunca la volvía a usar. Con una sola CPU, el sistema se moría en el primer lock contendido.

**Causa:** el handler de `MSG_MUTEX_LOCK` con mutex tomado encolaba al waiter pero no lo sacaba de `cola_exec` ni marcaba la CPU libre.

**Fix:** `sacar_de_exec(pid)` + `mover_a_block(proc)` en la rama bloqueante — el mismo camino que las syscalls de IO. Efecto colateral correcto: al pasar por `mover_a_block`, el waiter también queda cubierto por el timer de suspensión (un proceso que espera mucho un mutex se suspende, y `MUTEX_UNLOCK` ya sabe buscarlo en SUSP. BLOCK).

### 6.5 El proceso desaparecía tras una syscall no bloqueante (commit `7cf4eff`)

**Síntoma:** después de un `MUTEX_CREATE` (o un lock sobre mutex libre), el proceso no volvía a ejecutar nunca más.

**Causa:** la CPU devuelve el proceso con motivo SYSCALL y el handler de la syscall lo atendía, pero nadie lo reinsertaba en READY: quedaba en el limbo (fuera de todas las colas).

**Fix:** la rama `MOTIVO_DEVOLUCION_SYSCALL` de `MSG_DEVOLVER_PROCESO` hace `cambiar_estado(READY)` + `encolar_en_ready`. También se aclaró el contrato con la CPU: para las syscalls de mutex la CPU **no espera** `MSG_OK` (commit `2e2fa9e` del lado CPU) — el redespacho llega por el planificador.

### 6.6 SLEEP escuchaba el op_code equivocado (commit `7ad11bc`)

**Síntoma:** las syscalls SLEEP se ignoraban con un warning de "op_code desconocido".

**Causa:** confusión entre los dos tramos del protocolo: la CPU envía **`MSG_SYSCALL_SLEEP`** (CPU→KS) pero el handler escuchaba **`MSG_IO_SLEEP`** (que es KS→IO). Mismo nombre conceptual, canal distinto.

**Fix:** corregir el case. De acá salió la convención de nombres del protocolo: prefijo `MSG_SYSCALL_*` para CPU→KS y `MSG_IO_*` para KS↔IO, para que el canal sea evidente en el nombre.

### 6.7 Corrupción de cola: `queue_size` durante un loop de `queue_pop` (commit `e40f3be`)

**Síntoma:** procesos que desaparecían de READY esporádicamente.

**Causa:** un loop de la forma `for (i = 0; i < queue_size(q); i++) { queue_pop(q); ... }` — la condición se reevalúa mientras el tamaño baja, así que recorre la mitad de los elementos.

**Fix:** capturar `int sz = queue_size(q)` antes del loop. El patrón quedó aplicado en todos los recorridos de colas del módulo (`sacar_de_exec`, `sacar_de_block`, etc.).

### 6.8 IO que termina tarde: faltaba SUSP. BLOCK → SUSP. READY (commit `beee701`)

**Síntoma:** si una IO terminaba *después* del timeout de suspensión, el proceso quedaba en SUSP. BLOCK para siempre.

**Causa:** el handler de `MSG_IO_FIN` solo buscaba el proceso en `cola_block`; si el timer de suspensión ya se lo había llevado, no lo encontraba y no hacía nada.

**Fix:** fallback a `sacar_de_susp_block`: si está ahí → SUSP. READY + señal al largo plazo. Es la carrera inherente entre el timer de suspensión y el fin de IO, resuelta a favor de "el que saca el proceso de la cola primero gana" (ambas extracciones son atómicas bajo el mutex de la cola, así que no hay doble procesamiento posible).

### 6.9 `queue_pop` sobre cola vacía en ks_mutex (commit `ea3385b`)

**Síntoma:** crash (segfault) al hacer unlock de un mutex sin waiters, detectado por los tests unitarios.

**Causa:** `queue_pop` de commons no es seguro sobre cola vacía.

**Fix:** guarda con `queue_size(...) > 0` antes de desencolar. Trivial, pero fue el primer bug que **cazaron los tests antes que la integración** — el argumento definitivo para mantener la lógica de mutexes separada de `main.c` y testeada.

### 6.10 Tests de IO colgaban la CI por byte order (commit `a6b9aa4`)

**Síntoma:** el job de CI de GitHub Actions quedaba colgado (timeout) en los tests de IO.

**Causa:** un test armaba un payload con los enteros en host byte order; el código leía con `ntohl` y obtenía un `n_bytes` gigantesco, quedándose esperando megabytes que nunca llegarían.

**Fix:** `htonl` en la construcción del payload del test. Moraleja que después reapareció en STDIN (ver 6.11): **el byte order es parte del contrato de cada mensaje y hay que documentarlo campo por campo**.

### 6.11 La convención de byte order de `MSG_IO_STDIN_DATOS`

Relacionado con lo anterior: el módulo IO devuelve `pid` y `n_bytes` en **host byte order** (les aplica `ntohl` antes de enviar), a diferencia de todo el resto del protocolo que viaja en network order. En lugar de cambiar el módulo IO (ya probado), se documentó la excepción en el handler del KS, que los lee sin conversión. No es elegante, pero está explícito en comentarios en ambos lados. Deuda técnica asumida conscientemente.

### 6.12 Los merges de CK3: handlers duplicados y regresiones evitadas

La integración de CK3 (junio 2026) fue el momento de mayor riesgo del módulo: tres ramas (`mutex-cmn` de Nicolas, `kenrel-scheduler` de Santiago, `impactante` de Kevin) tocaban `main.c` a la vez. Problemas concretos del merge y su resolución (commits `4ffecff`, `0c2d3c3`, informe del 18/06):

- **Dos implementaciones de `thread_km_listener`** (una por rama) → se unificaron en una sola que maneja los 4 op_codes de respuesta + los 3 eventos asincrónicos.
- **Handlers duplicados** de `MSG_INIT_PROC`, `MSG_MUTEX_CREATE`, `MSG_MUTEX_LOCK` (el switch compilaba con dos `case` iguales en versiones intermedias) → eliminados.
- Referencias a variables que ya no existían (`cola_ready` singular vs. `colas_ready[]` de CMN) en `manejar_bsod` y `manejar_mas_memoria` → corregidas a la estructura multinivel.
- Una rama de integración paralela (`integracion-cpu-check3`) proponía cambios que **eliminaban** `ks_cmn.c`, `ks_compact.c` y los tests — se detectó en el audit del 20/06 y **no se mergeó**, evitando regresar CMN, herencia y compactación completas.

La lección de proceso: el informe de estado con audit de requisitos (`estado_ck3_18-06-2026.md`) — una tabla requisito-por-requisito contra el enunciado — fue lo que permitió detectar tanto los bugs restantes como el merge peligroso antes de la entrega.

---

## 7. Decisiones de diseño

Resumen de las decisiones estructurales y su justificación:

1. **FIFO/RR como caso degenerado de CMN** (`n_colas = 1`): un solo planificador para los tres algoritmos. Menos código, menos ramas, los tres modos comparten los mismos fixes.

2. **Un hilo lector por socket, sin excepciones**, y el trabajo bloqueante siempre en hilos efímeros detached. Es la regla que salió de los deadlocks 6.1–6.3. El costo (muchos hilos cortos) es irrelevante a esta escala; el beneficio es que el razonamiento sobre deadlocks se vuelve local: basta chequear que ningún handler inline haga `km_request`.

3. **`km_request` serializado con un solo request en vuelo** en lugar de IDs de correlación: elegimos el protocolo más simple que no se puede desincronizar. El throughput hacia KM no fue nunca el cuello de botella (KM además simula delays de memoria mucho mayores).

4. **Timers como hilos que duermen** (`thread_quantum_timer`, `thread_suspension_timer`) en lugar de una rueda de timers central: un hilo por evento, con verificación atómica post-sueño ("¿el proceso sigue donde lo dejé?"). Desperdicia hilos pero elimina toda la contabilidad de cancelación: un timer obsoleto simplemente no encuentra a su proceso y muere.

5. **Estado de planificación mínimo en el KS** (`t_proceso` de 7 campos): el contexto pesado vive en KM. El KS mueve PIDs entre colas; nunca toca registros ni memoria. La separación de responsabilidades del enunciado llevada a la estructura de datos.

6. **Extracciones atómicas de colas como primitiva de resolución de carreras**: `sacar_de_block`, `sacar_de_exec`, `sacar_de_susp_block` buscan-y-sacan bajo el mutex de la cola. Cualquier carrera entre dos eventos sobre el mismo proceso (fin de IO vs. timeout de suspensión, quantum vs. syscall) la gana el que extrae primero, y el perdedor no encuentra nada y no hace nada. No hay flags de "ya procesado" ni double-checking.

7. **Direcciones lógicas (no físicas) en STDIN/STDOUT** (issue #64): desviación deliberada de la letra del enunciado, porque una dirección física cacheada se invalida con la compactación mientras el proceso está bloqueado. KM, dueño de las tablas, traduce en el momento del acceso.

8. **Lógica pura separada de `main.c`** (`ks_mutex`, `ks_cmn`, `ks_compact`) para testear sin sockets. Los bugs 6.9 y el ruteo del listener se cazaron en tests unitarios, no en integración.

---

## 8. Logs obligatorios

Todos los logs mínimos del enunciado, dónde se emiten y cuándo:

| Log | Dónde se emite |
|---|---|
| `## Conectado a Kernel Memory` | `main()`, tras el handshake con KM |
| `## CPU <ID> Conectada` | `atender_cliente`, al identificarse una CPU |
| `## (<PID>) Se crea el proceso - Estado: NEW` | `crear_proceso` |
| `## (<PID>) Pasa del estado <X> al estado <Y>` | `cambiar_estado` — **toda** transición pasa por acá |
| `## (<PID>) - Solicitó syscall: <NOMBRE>` | cada handler de syscall en `atender_cpu` |
| `## (<PID>) finalizó IO y pasa a READY / SUSP. READY` | `atender_io`, según dónde estaba el proceso |
| `## (<PID>) Toma el Mutex <NOMBRE>` | `mutex_ks_lock` (mutex libre) y `mutex_ks_unlock` (herencia FIFO del waiter) |
| `## (<PID>) Libera el Mutex <NOMBRE>` | `mutex_ks_unlock` |
| `## <PID> Cambio de prioridad: <vieja> - <nueva>` | herencia (elevación en LOCK) y restauración (en UNLOCK) |
| `## (<PID>) - Desalojado por fin de quantum` | rama INTERRUPCION sin `preemptado` de `MSG_DEVOLVER_PROCESO` |
| `## (<PID>) Prioridad: <P> - Desalojado por cola más prioritaria por el proceso <PID2> con prioridad <P2>` | `verificar_preemption` |
| `## Inicio de compactación` / `## Fin de compactación` | `handle_compactar` |
| `## (<PID>) finalizó su ejecución con motivo de <MOTIVO>` | EXIT / SEG_FAULT / BSOD, en sus respectivos caminos |

Motivos de finalización usados: `EXIT` (syscall o devolución normal), `SEG_FAULT` (la MMU de la CPU detectó un acceso fuera de límite y devolvió con motivo ERROR), `BSOD` (memoria corrupta).

---

## 9. Archivo de configuración

| Campo | Tipo | Uso en el código |
|---|---|---|
| `LOG_LEVEL` | String | nivel del logger de commons |
| `KERNEL_MEMORY_IP` / `KERNEL_MEMORY_PORT` | String / Número | conexión saliente a KM |
| `KERNEL_SCHEDULER_PORT` | Número | puerto del servidor propio (CPUs e IOs) |
| `PLANIFICATION_ALGORITHM` | String | `FIFO` \| `RR` \| `CMN` |
| `QUEUES_ALGORITHMS` | Lista | solo con CMN — algoritmo por cola, ej. `[FIFO,RR,RR]` → 3 colas (parseado por `parsear_queues_algorithms`) |
| `RR_QUANTUM` | Número (ms) | duración del quantum en colas RR |
| `QUEUE_PREEMPTION` | String | solo con CMN — `TRUE`/`FALSE`, desalojo entre colas |
| `SUSPENSION_TIMEOUT` | Número (ms) | tiempo máximo en BLOCK antes de suspender |

Ejemplo CMN completo (del enunciado):

```
LOG_LEVEL=INFO
PLANIFICATION_ALGORITHM=CMN
QUEUES_ALGORITHMS=[FIFO,RR,RR,FIFO,RR,FIFO]
RR_QUANTUM=1500
QUEUE_PREEMPTION=TRUE
SUSPENSION_TIMEOUT=35000
```

Nota: `kernel_scheduler.config.example` del repo trae el caso simple (FIFO); para CMN hay que agregar `QUEUES_ALGORITHMS` y `QUEUE_PREEMPTION`, que el código solo lee cuando el algoritmo es `CMN`.

---

## 10. Testing

**Tests unitarios** (cspecs, `make test` → `./bin/kernel_scheduler_tests`):

- `test_ks_mutex.c` — create/lock/unlock, orden FIFO de waiters, herencia (elevación y restauración de prioridades), unlock por no-owner, unlock sin waiters (el caso del bug 6.9).
- `test_ks_cmn.c` — parseo de `QUEUES_ALGORITHMS`: listas válidas, espacios, corchetes, casos borde.
- `test_ks_compact.c` — ruteo del listener de KM con la versión inyectable (`km_listener_run`): que `MSG_COMPACTAR` dispare el callback y el resto vaya a la condvar.

**CI:** GitHub Actions compila todos los módulos y corre los tests del KS en cada push (job agregado en `8c355f0`). La CI cazó el hang de byte order (6.10).

**Prueba de integración mínima** (documentada en el informe del 20/06): KM + KS + 1 CPU + script `scripts/0.txt` (`MUTEX_CREATE → MUTEX_LOCK → SLEEP → MUTEX_UNLOCK → EXIT`), verificando la secuencia completa de logs y transiciones de estado. Para validar tiempos reales de SLEEP hay que levantar además la IO SLEEP (sin ella el proceso sale de BLOCK de inmediato).

---

## 11. Bugs de runtime corregidos en la rama `fix/ks-bugs-runtime` (06/07/2026)

La auditoría posterior a esta documentación encontró bugs que rompían en tiempo de ejecución. Se corrigieron todos en la rama `fix/ks-bugs-runtime` y se verificaron con dos pruebas de integración reales (KM + MS + Swap + KS + CPU + IO): una que fuerza una **compactación completa** (MEM_ALLOC×3, MEM_FREE, MEM_ALLOC que no cabe contiguo) más INIT_PROC/mutex/EXIT, y otra que recorre el **ciclo de suspensión entero** (BLOCK → SUSP. BLOCK → SUSP. READY → READY → EXEC → EXIT con segmentos yendo y volviendo de Swap).

### Corregidos en el KS (`kernel_scheduler/src/main.c`, `proceso.h`)

1. **Doble free en MEM_ALLOC/MEM_FREE**: los cases liberaban el mensaje y el final del loop lo liberaba otra vez — abort de glibc en la primera syscall de memoria. Quedó un único free al final del loop.
2. **Deadlock determinístico de compactación (lado KS)**: `handle_compactar` enviaba el `MSG_FIN_COMPACTACION` con `km_request`, pero `mutex_km_req` lo retenía el `manejar_mem_alloc` que disparó la compactación → espera circular. Ahora el FIN se envía crudo (con `mutex_km_send`) y `handle_compactar` espera `sem_fin_compactacion`, que señala el hilo de MEM_ALLOC al recibir su respuesta (KM solo responde después de compactar).
3. **Race de conteo en compactación**: solo la devolución por interrupción posteaba `sem_cpus_devueltas`; un proceso que hacía syscall/EXIT en la ventana del desalojo dejaba la compactación esperando para siempre. La contabilidad se centralizó en `sacar_de_exec` (y `sacar_proc_de_exec_para_mem`): cualquier vía de salida de EXEC de un proceso marcado cuenta.
4. **Contrato MEM_ALLOC/FREE con la CPU**: la CPU no espera `MSG_OK` — devuelve el proceso y espera un despacho. El KS ahora redespacha con `MSG_DESPACHAR_PROCESO` a la misma CPU (`redespachar_a_misma_cpu`), lo que además refresca la tabla de segmentos vía `RESTAURAR_CONTEXTO` (clave post-compactación). Si el alloc falla incluso tras compactar, el proceso finaliza con ERROR.
5. **Carrera del DEVOLVER (doble despacho)**: la CPU envía `[SYSCALL][DEVOLVER(SYSCALL)]`; si el DEVOLVER se procesaba después de que el proceso fuera re-planificado, lo sacaba de EXEC y se despachaba dos veces (llegando a ejecutar instrucciones más allá del EXIT). Fix: `consumir_devolver()` inline en SLEEP/STDOUT/STDIN/MUTEX_LOCK/MEM_* — el orden en el socket lo garantiza la CPU.
6. **EXIT mataba a la CPU**: el handler respondía un `MSG_OK` que la CPU no espera; le llegaba en `recibir_proceso_a_ejecutar` y la CPU terminaba. Se eliminó el envío (y la CPU ahora tolera mensajes rezagados, ver abajo).
7. **CPU desconectada**: se remueve de `lista_cpus` (antes se le seguían despachando procesos a un socket muerto) y se rescata el proceso huérfano de `cola_exec` → READY.
8. **IO desconectada**: `fd_io_*` vuelve a `-1`; una reconexión lo reemplaza.
9. **`cmp_suspension` invertido**: des-suspendía primero a los *menos* prioritarios; ahora `prioridad <` (0 = máxima) como pide el enunciado.
10. **Timer de quantum obsoleto**: `gen_despacho` en `t_proceso` — un timer solo interrumpe al despacho que lo originó.
11. **Frente de READY perdido**: si no había CPU libre, el planificador devolvía el proceso al fondo; ahora lo reinserta al frente de la cola de la que salió (preserva el requisito de compactación).
12. Menores: MUTEX_LOCK sobre mutex inexistente finaliza el proceso (antes desaparecía del sistema); INIT_PROC responde `MSG_ERROR` si `crear_proceso` falla; rama duplicada muerta de `MOTIVO_DEVOLUCION_ERROR` eliminada.

### Corregidos en los otros módulos (descubiertos al verificar por integración)

13. **KM — deadlock determinístico de compactación (lado KM)**: el hilo único de la conexión KS se bloqueaba en `sem_wait` esperando un `MSG_FIN_COMPACTACION` que llega **por esa misma conexión que solo él lee**. Ahora el CREAR_SEGMENTO que necesita compactar queda "pendiente" y se responde desde el handler del FIN.
14. **KM — des-suspensión respondía `MSG_COMPACTAR`**: violaba el enunciado (la des-suspensión no debe compactar) y desincronizaba el protocolo con el KS. Ahora responde `MSG_ERROR` ("sin espacio").
15. **KM — dos lectores por socket**: los hilos `atender_cliente` de MS y Swap competían con `ms_leer`/`ms_escribir`/`swap_*` por las respuestas. Esas conexiones quedan pasivas tras la identificación.
16. **Memory Stick — nunca escuchaba a KM**: tras el handshake nadie leía `fd_km`; todo acceso físico de KM (compactación, STDOUT/STDIN, suspensión) colgaba. Nuevo hilo `atender_kernel_memory`.
17. **CPU — mensajes rezagados fatales**: `recibir_proceso_a_ejecutar` terminaba la CPU ante cualquier op_code inesperado; ahora los ignora con warning.

### Deuda técnica que sigue pendiente

- **KM nunca envía `MSG_BSOD`**: la desconexión de un Memory Stick con datos no se detecta (el KS tiene el handler listo, pero KM no lo dispara). El hot-unplug del enunciado está sin implementar del lado KM.
- **`MOTIVO_DEVOLUCION_SEG_FAULT` sin uso**: la CPU reporta segfaults con `MOTIVO_DEVOLUCION_ERROR`; el enum sugiere una distinción que no existe.
- **Byte order no uniforme en `MSG_IO_STDIN_DATOS`** (ver 6.11): excepción documentada.
- **Una sola IO por tipo**: una segunda IO del mismo tipo pisa a la primera (alcanza para el enunciado).
- **Logs obligatorios de KM** (issue #53): la suspensión/des-suspensión en KM no loguea con el formato del enunciado.

---

*Documento generado el 05/07/2026 a partir del código en `main` (HEAD `dc42d28`); sección 11 actualizada el 06/07/2026 con los fixes de la rama `fix/ks-bugs-runtime`.*
