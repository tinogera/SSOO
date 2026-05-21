# Instalación, despliegue y prueba — Plug & Pray

## Índice

1. [Prerequisitos](#1-prerequisitos)
2. [Obtener el repositorio](#2-obtener-el-repositorio)
3. [Compilar los módulos](#3-compilar-los-módulos)
4. [Configurar cada módulo](#4-configurar-cada-módulo)
5. [Descubrir las IPs de las VMs del laboratorio](#5-descubrir-las-ips-de-las-vms-del-laboratorio)
6. [Orden de inicio](#6-orden-de-inicio)
7. [Despliegue distribuido en VMs](#7-despliegue-distribuido-en-vms)
8. [Scripts de proceso](#8-scripts-de-proceso)
9. [Prueba de integración CK2](#9-prueba-de-integración-ck2)

---

## 1. Prerequisitos

En cada VM se necesita:

```bash
# so-commons-library (una sola vez por máquina)
git clone https://github.com/sisoputnfrba/so-commons-library.git
cd so-commons-library
make install
cd ..

# Herramientas de compilación
sudo apt-get install build-essential
```

---

## 2. Obtener el repositorio

```bash
git clone git@github.com:sisoputnfrba/tp-2026-1c-Impactante.git
cd tp-2026-1c-Impactante
```

Si la VM no tiene clave SSH configurada, usar HTTPS:

```bash
git clone https://github.com/sisoputnfrba/tp-2026-1c-Impactante.git
```

---

## 3. Compilar los módulos

Cada módulo tiene su propio Makefile. Compilar solo los que se van a ejecutar en esa VM:

```bash
make -C utils          # siempre primero — es la librería compartida
make -C kernel_memory
make -C kernel_scheduler
make -C cpu
make -C io
make -C memory_stick
make -C swap
```

Los binarios quedan en `{modulo}/bin/`.

---

## 4. Configurar cada módulo

Cada módulo trae un archivo `.config.example` con todas las claves disponibles y sus valores por defecto. Copiarlo y editarlo:

```bash
cp kernel_memory/kernel_memory.config.example   kernel_memory/kernel_memory.config
cp kernel_scheduler/kernel_scheduler.config.example  kernel_scheduler/kernel_scheduler.config
cp cpu/cpu.config.example                        cpu/cpu.config
cp io/io.config.example                          io/io.config
cp memory_stick/memory_stick.config.example      memory_stick/memory_stick.config
cp swap/swap.config.example                      swap/swap.config
```

Luego editar cada `.config` reemplazando `127.0.0.1` por la IP real de la VM donde corra ese módulo (ver sección 5).

---

## 5. Descubrir las IPs de las VMs del laboratorio

Las VMs del laboratorio no tienen IP fija asignada de antemano. Al iniciar sesión en cada una, ejecutar:

```bash
ip addr show
```

Buscar la interfaz de red activa (generalmente `ens33` o `eth0`). El bloque `inet` muestra la IP:

```
2: ens33: <BROADCAST,MULTICAST,UP,LOWER_UP>
    inet 10.100.3.47/24 brd 10.100.3.255 scope global ens33
```

En ese ejemplo la IP es `10.100.3.47`.

Alternativa más corta:

```bash
hostname -I
```

Devuelve todas las IPs de la máquina, separadas por espacio. Tomar la primera.

Una vez que cada integrante tenga la IP de su VM, completar la siguiente tabla y compartirla en el grupo:

| Módulo | VM | IP |
|---|---|---|
| Kernel Memory + Swap | VM1 | `__________` |
| Kernel Scheduler | VM2 | `__________` |
| CPU | VM3 | `__________` |
| IO | VM4 | `__________` |
| Memory Stick | VM5 | `__________` |

---

## 6. Orden de inicio

Los módulos deben iniciarse en este orden porque algunos esperan que otros ya estén escuchando:

```
1. Kernel Memory     (no depende de nadie)
2. Swap              (conecta a KM)
3. Memory Stick      (conecta a KM)
4. Kernel Scheduler  (conecta a KM; luego espera CPU e IO)
5. IO SLEEP          (conecta a KS)
6. IO STDOUT         (conecta a KS)
7. IO STDIN          (conecta a KS)
8. CPU               (conecta a KS y KM — último en iniciar)
```

> Si un módulo no puede conectarse al destino, reintentará o terminará con error. Verificar que el módulo destino esté levantado antes de iniciar el cliente.

---

## 7. Despliegue distribuido en VMs

### Ejemplo de IPs (reemplazar con las reales el día de la entrega)

| Módulo | IP ejemplo |
|---|---|
| Kernel Memory | `10.100.3.10` |
| Kernel Scheduler | `10.100.3.11` |
| CPU | `10.100.3.12` |
| IO | `10.100.3.13` |
| Memory Stick | `10.100.3.14` |

### VM1 — Kernel Memory + Swap

```bash
# kernel_memory.config
KERNEL_MEMORY_PORT=37215
SCRIPTS_BASEPATH=/home/utnso/scripts
INSTRUCTION_DELAY=0

# swap.config
KERNEL_MEMORY_IP=127.0.0.1   # mismo host
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

```bash
# kernel_scheduler.config
LOG_LEVEL=INFO
KERNEL_MEMORY_IP=10.100.3.10   # IP de VM1
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

```bash
# cpu.config
IP_KERNEL=10.100.3.11    # IP de VM2
PUERTO_KERNEL=37214
IP_MEMORY=10.100.3.10    # IP de VM1
PUERTO_MEMORY=37215
```

```bash
./cpu/bin/cpu cpu/cpu.config 1
# El segundo argumento es el ID de esta CPU (número entero, único por instancia)
```

### VM4 — IO

```bash
# io.config
LOG_LEVEL=INFO
KERNEL_SCHEDULER_IP=10.100.3.11   # IP de VM2
KERNEL_SCHEDULER_PORT=37214
```

```bash
./io/bin/io io/io.config SLEEP  &
./io/bin/io io/io.config STDOUT &
./io/bin/io io/io.config STDIN  &
```

### VM5 — Memory Stick

```bash
# memory_stick.config
LOG_LEVEL=INFO
MEMORY_STICK_PORT=37216
KERNEL_MEMORY_IP=10.100.3.10   # IP de VM1
KERNEL_MEMORY_PORT=37215
MEMORY_DELAY=500
```

```bash
./memory_stick/bin/memory_stick memory_stick/memory_stick.config 1024
# El segundo argumento es el tamaño del buffer en bytes
```

---

## 8. Scripts de proceso

Los scripts están en el directorio `scripts/`. Cada archivo se llama `{PID}.txt` y contiene una instrucción por línea.

El KS recibe como argumento la ruta del script del proceso inicial. El KM construye la ruta automáticamente para los procesos subsiguientes como `{SCRIPTS_BASEPATH}/{PID}.txt`.

Copiar los scripts al `SCRIPTS_BASEPATH` configurado en el KM:

```bash
mkdir -p /home/utnso/scripts
cp scripts/*.txt /home/utnso/scripts/
```

Instrucciones disponibles: `SLEEP`, `STDOUT`, `STDIN`, `MUTEX_CREATE`, `MUTEX_LOCK`, `MUTEX_UNLOCK`, `EXIT`. Ver `scripts/README.md` para detalles.

---

## 9. Prueba de integración CK2

### Verificar conexiones

Al levantar los módulos en orden, cada uno loguea su conexión exitosa. Verificar que aparezcan:

```
[KM]  Kernel Memory escuchando en puerto 37215
[KS]  Kernel Memory conectado
[KS]  CPU N conectada
[KS]  IO SLEEP conectado
[KS]  IO STDOUT conectado
[KS]  IO STDIN conectado
[CPU] Conectado a Kernel Scheduler
[CPU] Conectado a Kernel Memory
```

### Prueba FIFO básica

1. Configurar `PLANIFICATION_ALGORITHM=FIFO` en el KS.
2. Lanzar con el script `scripts/0.txt` (incluye `SLEEP`, `MUTEX_CREATE/LOCK/UNLOCK` y `EXIT`).
3. Verificar en los logs del KS que el proceso pase por los estados: `NEW → READY → EXEC → BLOCK → READY → EXEC → EXIT`.

### Prueba Round Robin

1. Configurar `PLANIFICATION_ALGORITHM=RR` y `RR_QUANTUM=1000`.
2. Usar un script con varias instrucciones (`SLEEP 3000` provoca múltiples quantums).
3. Verificar en logs del KS que aparezca `Desalojado por fin de quantum`.

### Prueba SUSPENSION_TIMEOUT

1. Configurar `SUSPENSION_TIMEOUT=2000` (2 segundos).
2. Usar un script con `SLEEP 10000` (10 segundos).
3. Verificar que el proceso pase a `SUSP. BLOCK` a los 2 segundos, y luego a `SUSP. READY → READY` cuando el SLEEP termina.

### Prueba Mutex

Crear dos scripts que compitan por el mismo mutex:

```
# scripts/0.txt        # scripts/1.txt
MUTEX_CREATE mutex1    MUTEX_LOCK mutex1
MUTEX_LOCK mutex1      SLEEP 1000
SLEEP 3000             MUTEX_UNLOCK mutex1
MUTEX_UNLOCK mutex1    EXIT
EXIT
```

Verificar que el proceso que llega segundo a `MUTEX_LOCK` queda en BLOCK hasta que el primero hace `MUTEX_UNLOCK`.
