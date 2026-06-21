# Instalación, despliegue y prueba — Plug & Pray

**Sistemas Operativos — UTN FRBA — Equipo Impactante**

---

## Índice

1. [Sistema operativo](#1-sistema-operativo)
2. [Instalar dependencias](#2-instalar-dependencias)
3. [Obtener el repositorio](#3-obtener-el-repositorio)
4. [Compilar los módulos](#4-compilar-los-módulos)
5. [Verificar la instalación](#5-verificar-la-instalación)
6. [Configurar cada módulo](#6-configurar-cada-módulo)
7. [Descubrir las IPs de las VMs del laboratorio](#7-descubrir-las-ips-de-las-vms-del-laboratorio)
8. [Orden de inicio](#8-orden-de-inicio)
9. [Despliegue distribuido en VMs](#9-despliegue-distribuido-en-vms)
10. [Scripts de proceso](#10-scripts-de-proceso)
11. [Prueba de integración CK2](#11-prueba-de-integración-ck2)
12. [Pruebas preliminares](#12-pruebas-preliminares)
13. [Cambios introducidos en v1.1 del enunciado](#13-cambios-introducidos-en-v11-del-enunciado)

---

## 1. Sistema operativo

| Entorno | SO | Uso |
|---|---|---|
| Desarrollo | Xubuntu 22.04 LTS (64-bit) | Máquinas de los integrantes del equipo |
| Evaluación | Ubuntu Server 22.04 LTS (64-bit) | VMs de la cátedra durante el coloquio |

Los pasos de instalación son idénticos en ambos entornos. La única diferencia es que Ubuntu Server **no tiene entorno gráfico**, por lo que todo se opera por terminal.

---

## 2. Instalar dependencias

### Herramientas de compilación

```bash
sudo apt install -y build-essential make git libreadline-dev
```

| Paquete | Descripción |
|---|---|
| `build-essential` | GCC y utilidades de compilación |
| `make` | Sistema de build usado por todos los módulos |
| `git` | Control de versiones |
| `libreadline-dev` | Lectura de línea con historial (usada por IO STDIN) |

### so-commons-library (biblioteca de la cátedra)

No está en APT; se instala manualmente:

```bash
git clone https://github.com/sisoputnfrba/so-commons-library.git
cd so-commons-library
make install
cd ..
rm -rf so-commons-library   # opcional: limpiar el directorio de instalación
```

Esto instala:
- Headers en `/usr/include/commons/`
- Biblioteca compartida en `/usr/lib/libcommons.so`

> Si ya está instalada (`ls /usr/lib/libcommons.so` no da error), no es necesario reinstalar.

### Resumen: instalación completa en una máquina limpia

```bash
# 1. Herramientas y dependencias
sudo apt install -y build-essential make git libreadline-dev

# 2. so-commons-library
git clone https://github.com/sisoputnfrba/so-commons-library.git
cd so-commons-library && make install && cd ..
rm -rf so-commons-library
```

---

## 3. Obtener el repositorio

Con clave SSH configurada:

```bash
git clone git@github.com:sisoputnfrba/tp-2026-1c-Impactante.git
cd tp-2026-1c-Impactante
```

Sin clave SSH (HTTPS):

```bash
git clone https://github.com/sisoputnfrba/tp-2026-1c-Impactante.git
cd tp-2026-1c-Impactante
```

---

## 4. Compilar los módulos

`utils` debe compilarse primero porque todos los demás módulos dependen de ella:

```bash
make -C utils
make -C kernel_memory
make -C kernel_scheduler
make -C cpu
make -C io
make -C memory_stick
make -C swap
```

En una VM que solo va a correr algunos módulos, compilar únicamente los necesarios (siempre incluir `utils`).

Los binarios quedan en `{modulo}/bin/`. `utils` genera `utils/lib/libutils.a`, que se enlaza estáticamente.

---

## 5. Verificar la instalación

```bash
# libcommons instalada
ls /usr/lib/libcommons.so

# libreadline instalada
ls /usr/lib/x86_64-linux-gnu/libreadline.so

# gcc y make disponibles
gcc --version
make --version
```

---

## 6. Configurar cada módulo

Cada módulo trae un archivo `*.config.example` con todas las claves y sus valores por defecto. Copiar y editar antes de ejecutar:

```bash
cp kernel_memory/kernel_memory.config.example  kernel_memory/kernel_memory.config
cp kernel_scheduler/kernel_scheduler.config.example kernel_scheduler/kernel_scheduler.config
cp cpu/cpu.config.example        cpu/cpu.config
cp io/io.config.example          io/io.config
cp memory_stick/memory_stick.config.example memory_stick/memory_stick.config
cp swap/swap.config.example      swap/swap.config
```

Los archivos `.config` **no se commitean** (están en `.gitignore`). Cada integrante mantiene el suyo local con las IPs y puertos de su entorno.

### Claves por módulo

| Módulo | Claves principales |
|---|---|
| `kernel_memory` | `KERNEL_MEMORY_PORT`, `SCRIPTS_BASEPATH`, `INSTRUCTION_DELAY` |
| `kernel_scheduler` | `KERNEL_MEMORY_IP/PORT`, `KERNEL_SCHEDULER_PORT`, `PLANIFICATION_ALGORITHM`, `RR_QUANTUM`, `SUSPENSION_TIMEOUT` |
| `cpu` | `IP_KERNEL`, `PUERTO_KERNEL`, `IP_MEMORY`, `PUERTO_MEMORY` |
| `io` | `KERNEL_SCHEDULER_IP`, `KERNEL_SCHEDULER_PORT` |
| `memory_stick` | `MEMORY_STICK_PORT`, `KERNEL_MEMORY_IP/PORT`, `MEMORY_DELAY` |
| `swap` | `KERNEL_MEMORY_IP/PORT`, `SWAP_FILE_PATH`, `SWAP_FILE_SIZE`, `BLOCK_SIZE` |

### Puertos por defecto

| Módulo | Puerto | Quién se conecta |
|---|---|---|
| Kernel Memory | `37215` | KS, CPU, MS, Swap |
| Kernel Scheduler | `37214` | CPU, IO |
| Memory Stick | `37216` | CPU |

---

## 7. Descubrir las IPs de las VMs del laboratorio

Las VMs del laboratorio no tienen IP fija asignada de antemano. Al iniciar sesión en cada una, ejecutar:

```bash
ip addr show
```

Buscar la interfaz activa (generalmente `ens33` o `eth0`). El campo `inet` muestra la IP:

```
2: ens33: <BROADCAST,MULTICAST,UP,LOWER_UP>
    inet 10.100.3.47/24 brd 10.100.3.255 scope global ens33
```

Alternativa más corta:

```bash
hostname -I
```

Devuelve todas las IPs separadas por espacio. Tomar la primera.

Una vez que cada integrante tenga la IP de su VM, completar esta tabla y compartirla en el grupo antes de arrancar:

| Módulo | VM | IP |
|---|---|---|
| Kernel Memory + Swap | VM1 | `__________` |
| Kernel Scheduler | VM2 | `__________` |
| CPU | VM3 | `__________` |
| IO | VM4 | `__________` |
| Memory Stick | VM5 | `__________` |

---

## 8. Orden de inicio

Los módulos deben levantarse en este orden porque cada uno espera que sus dependencias ya estén escuchando:

```
1. Kernel Memory     — no depende de nadie, todos se conectan a él
2. Swap              — conecta a KM
3. Memory Stick      — conecta a KM
4. Kernel Scheduler  — conecta a KM; luego espera conexiones de CPU e IO
5. IO SLEEP          — conecta a KS
6. IO STDOUT         — conecta a KS
7. IO STDIN          — conecta a KS
8. CPU               — conecta a KS y KM (último en iniciar)
```

> Si un módulo falla al conectarse, verificar que el módulo destino esté levantado y que las IPs y puertos en los `.config` sean correctos.

---

## 9. Despliegue distribuido en VMs

### Ejemplo de IPs (reemplazar con las reales el día de la entrega)

| Módulo | IP ejemplo |
|---|---|
| Kernel Memory | `10.100.3.10` |
| Kernel Scheduler | `10.100.3.11` |
| CPU | `10.100.3.12` |
| IO | `10.100.3.13` |
| Memory Stick | `10.100.3.14` |

### VM1 — Kernel Memory + Swap

`kernel_memory.config`:
```
KERNEL_MEMORY_PORT=37215
SCRIPTS_BASEPATH=/home/utnso/scripts
INSTRUCTION_DELAY=0
```

`swap.config`:
```
KERNEL_MEMORY_IP=127.0.0.1
KERNEL_MEMORY_PORT=37215
SWAP_FILE_PATH=/tmp/tp_swap.bin
SWAP_FILE_SIZE=4096
BLOCK_SIZE=64
```

```bash
./kernel_memory/bin/kernel_memory kernel_memory/kernel_memory.config &
./swap/bin/swap swap/swap.config &
```

### VM2 — Kernel Scheduler

`kernel_scheduler.config`:
```
LOG_LEVEL=INFO
KERNEL_MEMORY_IP=10.100.3.10
KERNEL_MEMORY_PORT=37215
KERNEL_SCHEDULER_PORT=37214
PLANIFICATION_ALGORITHM=FIFO
RR_QUANTUM=1000
SUSPENSION_TIMEOUT=5000
```

```bash
./kernel_scheduler/bin/kernel_scheduler kernel_scheduler/kernel_scheduler.config /home/utnso/scripts/0.txt
```

> El segundo argumento es la ruta al script del proceso inicial (PID 0).

### VM3 — CPU

`cpu.config`:
```
IP_KERNEL=10.100.3.11
PUERTO_KERNEL=37214
IP_MEMORY=10.100.3.10
PUERTO_MEMORY=37215
```

```bash
./cpu/bin/cpu cpu/cpu.config 1
# El segundo argumento es el ID de esta CPU (entero, único por instancia)
```

### VM4 — IO

`io.config`:
```
LOG_LEVEL=INFO
KERNEL_SCHEDULER_IP=10.100.3.11
KERNEL_SCHEDULER_PORT=37214
```

```bash
./io/bin/io io/io.config SLEEP  &
./io/bin/io io/io.config STDOUT &
./io/bin/io io/io.config STDIN  &
```

### VM5 — Memory Stick

`memory_stick.config`:
```
LOG_LEVEL=INFO
MEMORY_STICK_PORT=37216
KERNEL_MEMORY_IP=10.100.3.10
KERNEL_MEMORY_PORT=37215
MEMORY_DELAY=500
```

```bash
./memory_stick/bin/memory_stick memory_stick/memory_stick.config 1024
# El segundo argumento es el tamaño del buffer en bytes
```

---

## 10. Scripts de proceso

Los scripts están en `scripts/`. Cada archivo se llama `{PID}.txt` y tiene una instrucción por línea.

El KS recibe la ruta del script del proceso inicial como argumento. El KM construye la ruta de los demás procesos automáticamente como `{SCRIPTS_BASEPATH}/{PID}.txt`.

Copiar los scripts a la VM donde corre el KM:

```bash
mkdir -p /home/utnso/scripts
cp scripts/*.txt /home/utnso/scripts/
```

Instrucciones disponibles: `SLEEP`, `STDOUT`, `STDIN`, `MUTEX_CREATE`, `MUTEX_LOCK`, `MUTEX_UNLOCK`, `EXIT`. Ver `scripts/README.md` para el formato completo.

---

## 11. Prueba de integración CK2

### Verificar conexiones

Al levantar en orden, cada módulo loguea su conexión exitosa. Verificar que aparezcan:

```
[KM]  Kernel Memory escuchando en puerto 37215
[KS]  Conectado a Kernel Memory
[IO]  Conectado a Kernel Scheduler      (×3, uno por tipo)
[CPU] Conectado a Kernel Scheduler
[CPU] Conectado a Kernel Memory
```

### FIFO básico

1. `PLANIFICATION_ALGORITHM=FIFO` en el KS.
2. Lanzar con `scripts/0.txt` (tiene `SLEEP`, `MUTEX_CREATE/LOCK/UNLOCK`, `EXIT`).
3. En los logs del KS verificar la secuencia de estados: `NEW → READY → EXEC → BLOCK → READY → EXEC → EXIT`.

### Round Robin

1. `PLANIFICATION_ALGORITHM=RR`, `RR_QUANTUM=1000`.
2. Script con `SLEEP 3000` — dura más que el quantum.
3. En los logs del KS debe aparecer `Desalojado por fin de quantum`.

### SUSPENSION_TIMEOUT

1. `SUSPENSION_TIMEOUT=2000` (2 segundos).
2. Script con `SLEEP 10000` (10 segundos).
3. A los 2 segundos el proceso debe pasar a `SUSP. BLOCK`. Cuando el SLEEP termina, debe pasar a `SUSP. READY → READY`.

### Mutex con dos procesos

Scripts `0.txt` y `1.txt` compitiendo por el mismo mutex:

```
# 0.txt                  # 1.txt
MUTEX_CREATE mutex1      MUTEX_LOCK mutex1
MUTEX_LOCK mutex1        SLEEP 1000
SLEEP 3000               MUTEX_UNLOCK mutex1
MUTEX_UNLOCK mutex1      EXIT
EXIT
```

El proceso que llega segundo a `MUTEX_LOCK` debe quedar en BLOCK hasta que el primero ejecute `MUTEX_UNLOCK`.

---

## 12. Pruebas preliminares

Los scripts de prueba oficiales están en el repositorio `sisoputnfrba/plug-n-pray-pruebas`. Clonar junto al TP o copiar los `.prc` al `SCRIPTS_BASEPATH` del Kernel Memory antes de correr cada suite.

```bash
git clone https://github.com/sisoputnfrba/plug-n-pray-pruebas.git
```

### 12.1 Planificación preliminar

**Objetivo:** validar la planificación de corto plazo sin involucrar memoria.

**Requisitos de configuración:**
- 1 sola CPU conectada.
- `SUSPENSION_TIMEOUT` alto (p. ej. `60000`) para no entrar en suspensión durante la prueba. Si se quiere verificar la suspensión, bajar el valor a menos de `20000`.

**Scripts:**

| Archivo | Prioridad | Descripción |
|---|---|---|
| `PLANI_PRE_0.prc` | — | Script maestro. Lanza los subprocesos y termina con EXIT. |
| `PLANI_PRE_1.prc` | 3 (×2) | `SET` de registros + `SLEEP 20000` (dos veces) + EXIT. Activa la suspensión si `SUSPENSION_TIMEOUT < 20000`. |
| `PLANI_PRE_2.prc` | 2 (×2) | Countdown de AX=50 con `SUB AX BX` + `JNZ AX` — loop con salto condicional. |
| `PLANI_PRE_3.prc` | 1 (×1) | SET de todos los registros + `SLEEP 100` (repetido 4 veces) + EXIT. |

**Contenido de `PLANI_PRE_0.prc`:**
```
INIT_PROC PLANI_PRE_1.prc 3
INIT_PROC PLANI_PRE_1.prc 3
INIT_PROC PLANI_PRE_2.prc 2
INIT_PROC PLANI_PRE_2.prc 2
INIT_PROC PLANI_PRE_3.prc 1
EXIT
```

**Ejecución:** pasar `PLANI_PRE_0.prc` como script inicial al Kernel Scheduler.

**Resultado esperado:** los cinco procesos hijos se crean, ejecutan y terminan. En los logs del KS deben verse transiciones `NEW → READY → EXEC → EXIT` para cada uno. Si `SUSPENSION_TIMEOUT` es menor al `SLEEP` de `PLANI_PRE_1`, los procesos de prioridad 3 deben pasar a `SUSP. BLOCK` y luego regresar a `SUSP. READY → READY`.

---

### 12.2 Memoria preliminar

**Objetivo:** validar la creación, escritura, lectura y eliminación de segmentos de memoria.

**Requisitos de configuración:**
- Al menos 1 Memory Stick con buffer de **256 bytes**.
- `SEGMENT_MAX_SIZE=128` en el Kernel Memory.
- Para probar segmentos distribuidos entre varios Memory Sticks, levantar múltiples instancias con buffers más chicos (p. ej. 64 bytes cada una).

**Scripts:**

| Archivo | Descripción |
|---|---|
| `MEMORIA_PRE_0.prc` | Script maestro. Lanza `MEMORIA_PRE_3` (segfault inmediato), espera 10 segundos, luego lanza `MEMORIA_PRE_1` y `MEMORIA_PRE_2`. |
| `MEMORIA_PRE_1.prc` | Alloc de 4 segmentos de 32 B, escribe en cada uno con `MOV_OUT`, libera los segmentos 0 y 2, re-alloca 1 segmento de 128 B, lee con `MOV_IN`. Verifica compactación/reuso de espacio. |
| `MEMORIA_PRE_2.prc` | Alloc de 1 segmento de 64 B, lee por `STDIN` y escribe por `STDOUT`. Requiere IO STDIN e IO STDOUT activos. |
| `MEMORIA_PRE_3.prc` | `MEM_ALLOC` + `MOV_OUT` a un segmento sin espacio válido → termina con **segmentation fault**. Finalización esperada: el proceso termina de forma abrupta. |

**Contenido de `MEMORIA_PRE_0.prc`:**
```
INIT_PROC MEMORIA_PRE_3.prc 1
SLEEP 10000
INIT_PROC MEMORIA_PRE_1.prc 2
INIT_PROC MEMORIA_PRE_2.prc 2
EXIT
```

**Ejecución:** pasar `MEMORIA_PRE_0.prc` como script inicial al Kernel Scheduler.

**Resultado esperado:**
1. `MEMORIA_PRE_3` termina rápidamente con segmentation fault (comportamiento correcto).
2. Tras los 10 segundos del `SLEEP`, se crean `MEMORIA_PRE_1` y `MEMORIA_PRE_2`.
3. `MEMORIA_PRE_1` completa el ciclo alloc → write → free → realloc → read sin errores.
4. `MEMORIA_PRE_2` bloquea en STDIN esperando input; al recibirlo, lo escribe por STDOUT y termina.

---

## 13. Cambios introducidos en v1.1 del enunciado

La versión 1.1 del enunciado (publicada 08/06/2026) introduce aclaraciones y correcciones que **impactan directamente en la implementación de CK3**. A continuación se detallan los cambios y su impacto técnico.

### 13.1 Syscalls deben liberar la CPU

**Sección afectada:** Kernel Scheduler — nueva sección "Atención de Syscalls".

Ante cualquier syscall, la CPU debe seguir este flujo sin excepción:

1. Incrementar el Program Counter (PC) en 1.
2. Guardar el contexto de ejecución en el Kernel Memory.
3. Enviar el PID al Kernel Scheduler (via `MSG_DEVOLVER_PROCESO` con motivo `SYSCALL`).
4. Quedar libre para ejecutar otro proceso.

El KS es quien decide cuándo redespachar el proceso (inmediatamente si la syscall es no bloqueante, o cuando se resuelva el bloqueo).

**Impacto en el código:**
- `cpu/src/cpu_syscalls.c`: las funciones `enviar_syscall_mutex_create`, `enviar_syscall_mutex_unlock` y `enviar_syscall_exit` actualmente esperan un `MSG_OK` del KS antes de retornar. Deben modificarse para **no esperar respuesta**.
- `kernel_scheduler/src/main.c`: para MUTEX_CREATE y MUTEX_UNLOCK, el KS ya no envía `MSG_OK` a la CPU. Redespacha al proceso vía el planificador normal.

### 13.2 MUTEX_LOCK bloqueante: el bloqueo pasa al KS

Con el cambio anterior, la CPU nunca queda bloqueada esperando un mutex. El bloqueo por `MUTEX_LOCK` cuando el mutex está tomado ahora es responsabilidad **exclusiva del KS**:

- Si el mutex está libre: KS asigna el mutex y redespacha el proceso inmediatamente.
- Si el mutex está tomado: KS mueve el proceso a `BLOCK` (no lo redespacha). Cuando se haga `MUTEX_UNLOCK`, el KS mueve el proceso de `BLOCK` a `READY`.

**Impacto:** la cola de waiters del mutex en `ks_mutex.c` ya no necesita guardar el `fd_cpu` — solo necesita guardar el PID del proceso en espera. El redespacho ocurre a través del planificador normal.

### 13.3 STDIN y STDOUT usan dirección física, no lógica

**Cambio de protocolo:** la CPU ahora envía al KS una **dirección física** (ya traducida por la MMU) en lugar de una dirección lógica.

| | v1.0 | v1.1 |
|---|---|---|
| CPU → KS (STDOUT) | `{ pid, dir_logica, tamanio }` | `{ pid, dir_fisica, tamanio }` |
| CPU → KS (STDIN) | `{ pid, dir_logica, tamanio }` | `{ pid, dir_fisica, tamanio }` |

**Impacto en el código:**
- `utils/src/utils/protocolo.h`: renombrar `direccion_logica` → `direccion_fisica` en `t_payload_syscall_io_memoria`.
- `cpu/src/cpu_ciclo.c`: antes de enviar STDOUT/STDIN al KS, traducir la dirección lógica (registros SI/DI) a dirección física usando la MMU.
- `kernel_scheduler/src/main.c`: al recibir STDOUT, pedir los bytes al KM con la dirección física recibida, luego reenviarlos a IO STDOUT. Al recibir STDIN, cuando IO devuelva los datos, pedirle al KM que los escriba en la dirección física.

### 13.4 Flujo real de STDOUT y STDIN con Kernel Memory

Con la dirección física disponible en el KS, el flujo completo para CK3 es:

**STDOUT:**
```
CPU → KS: MSG_SYSCALL_STDOUT { pid, dir_fisica, tamanio }
KS  → KM: MSG_KM_LEER_MEMORIA { dir_fisica, tamanio }
KM  → KS: MSG_KM_LEER_MEMORIA_RESP { bytes[] }
KS  → IO STDOUT: MSG_IO_STDOUT { pid, bytes[] }
IO  → KS: MSG_IO_FIN { pid }
KS: proceso BLOCK → READY
```

**STDIN:**
```
CPU → KS: MSG_SYSCALL_STDIN { pid, dir_fisica, tamanio }
KS  → IO STDIN: MSG_IO_STDIN { pid, n_bytes }
IO: lee del teclado
IO  → KS: MSG_IO_STDIN_DATOS { pid, n_bytes, datos[] }
KS  → KM: MSG_KM_ESCRIBIR_MEMORIA { dir_fisica, datos[] }
KM  → KS: MSG_OK
KS: proceso BLOCK → READY
```

### 13.5 Syscalls de memoria (MEM_ALLOC/MEM_FREE): reenviar a la misma CPU

Las syscalls relacionadas a memoria (`MEM_ALLOC`, `MEM_FREE`) tienen una regla especial: una vez que el KM resuelve la operación, el proceso **debe ser reenviado a la misma CPU que hizo la llamada**, no a cualquier CPU libre.

Adicionalmente, si al crear un segmento hay espacio suficiente pero no es contiguo, el KM debe disparar una compactación antes de asignar.

### 13.6 Orden FIFO en el desbloqueo de Mutex

Al liberar un mutex que tiene procesos en espera, los procesos deben desbloquearse **en el orden en que solicitaron el mutex** (FIFO). La implementación actual con `queue_push`/`queue_pop` ya cumple esto.

### 13.7 Des-suspensión usa algoritmo de búsqueda de huecos

Al des-suspender un proceso (restaurar segmentos de SWAP a memoria principal), el KM debe usar el **algoritmo de búsqueda de huecos configurado** (BEST FIT o WORST FIT) para ubicar los segmentos, en lugar de colocarlos en cualquier lugar disponible.

### 13.8 Direcciones físicas de Memory Sticks son globales

**Cambio importante:** se elimina la restricción de que las direcciones físicas de cada Memory Stick arranquen en 0. Las direcciones físicas son **globales** a todo el espacio de memoria del sistema.

**Impacto:**
- El KM lleva un registro de qué rango de dirección física corresponde a cada MS (p. ej., MS1 cubre 0–255, MS2 cubre 256–511).
- Cuando el KM recibe una lectura o escritura con una dirección física, calcula a qué MS(s) corresponde y divide la operación si cruza fronteras entre sticks.
- El Memory Stick recibe sus pedidos con offsets que reflejan la dirección global, no relativa.

### 13.9 IO tiene 1 solo hilo de ejecución

Cada módulo IO opera con **un único hilo** que atiende pedidos del KS de forma secuencial. La implementación actual ya cumple esto (el `main` de IO no crea hilos adicionales).
