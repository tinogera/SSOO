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
