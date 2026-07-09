# Instalación, despliegue y prueba — Plug & Pray

**Sistemas Operativos — UTN FRBA — Equipo Impactante**

---

## Índice

1. [Sistema operativo](#1-sistema-operativo)
2. [Instalar dependencias](#2-instalar-dependencias)
3. [Obtener el repositorio](#3-obtener-el-repositorio)
4. [Configurar cada módulo](#4-configurar-cada-módulo)
5. [Script de despliegue — deploy.sh](#5-script-de-despliegue--deploysh)
6. [Despliegue en una sola máquina (desarrollo)](#6-despliegue-en-una-sola-máquina-desarrollo)
7. [Despliegue distribuido en múltiples VMs](#7-despliegue-distribuido-en-múltiples-vms)
8. [Orden de inicio](#8-orden-de-inicio)
9. [Scripts de proceso (.prc)](#9-scripts-de-proceso-prc)
10. [Pruebas preliminares](#10-pruebas-preliminares)
11. [Cambios introducidos en v1.1 del enunciado](#11-cambios-introducidos-en-v11-del-enunciado)

---

## 1. Sistema operativo

| Entorno | SO | Uso |
|---|---|---|
| Desarrollo | Xubuntu / Lubuntu 22.04 LTS (64-bit) | Máquinas de los integrantes del equipo |
| Evaluación | Lubuntu 22.04 LTS (64-bit) | VMs de la cátedra durante el coloquio |

Los pasos de instalación son idénticos en ambos entornos.

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
rm -rf so-commons-library
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

## 4. Configurar cada módulo

Cada módulo tiene un archivo `*.config` en su directorio (ej. `kernel_memory/kernel_memory.config`). Ese archivo **ya existe en el repo** con valores de ejemplo para desarrollo local (`127.0.0.1`) — no hay que crearlo desde cero, solo editarlo con los valores correctos para el entorno donde vas a correr ese módulo (VM local, VM distribuida, etc).

> **Nota:** el `.gitignore` del proyecto tiene una regla `*.config` pensada para que cada quien mantenga su propia configuración sin pisar la de sus compañeros, pero los archivos `*.config` de cada módulo ya están commiteados en este repo (se agregaron antes de esa regla). Esto quiere decir que si editás `kernel_memory/kernel_memory.config` y hacés `git add`/`git commit`, ese cambio **sí** se sube y le pisa la configuración a todo el equipo. Para laboratorio o evaluación, evitar comitear esos cambios (`git status` antes de commitear, o `git checkout -- <archivo>.config` para descartar la edición local una vez que ya no la necesitás).

### Cómo editarlo

1. Abrir el archivo con cualquier editor de texto de consola, por ejemplo:
   ```bash
   nano kernel_memory/kernel_memory.config
   ```
   (`Ctrl+O` para guardar, `Ctrl+X` para salir. Si preferís `vim`: `vim kernel_memory/kernel_memory.config`, `i` para insertar, `Esc` luego `:wq` para guardar y salir.)
2. El formato es `CLAVE=VALOR`, una por línea, **sin espacios alrededor del `=`** (ej. `KERNEL_MEMORY_PORT=23841`, no `KERNEL_MEMORY_PORT = 23841`). Las líneas que empiezan con `#` son comentarios y se ignoran.
3. Modificar solo el valor de las claves que necesitás cambiar (típicamente las IPs, cuando pasás de local a distribuido — ver [Sección 7](#7-despliegue-distribuido-en-múltiples-vms)). No hace falta tocar las que ya están bien (ej. `LOG_LEVEL=INFO`).
4. Guardar el archivo. No hace falta recompilar ni reiniciar nada más que el módulo — los `.config` se leen al arrancar el proceso (`./deploy.sh <módulo>`).

Cada módulo también tiene un archivo `*.config.example` al lado (ej. `kernel_memory/kernel_memory.config.example`) con **cada clave comentada explicando qué significa**. Sirve como referencia rápida si no te acordás qué hace una clave; no hace falta copiarlo, ya que el `.config` real ya tiene la misma estructura.

### Claves por módulo

| Módulo | Claves principales |
|---|---|
| `kernel_memory` | `KERNEL_MEMORY_PORT`, `SCRIPTS_BASEPATH`, `SEGMENT_MAX_SIZE`, `INSTRUCTION_DELAY` |
| `kernel_scheduler` | `KERNEL_MEMORY_IP`, `KERNEL_MEMORY_PORT`, `KERNEL_SCHEDULER_PORT`, `PLANIFICATION_ALGORITHM`, `RR_QUANTUM`, `SUSPENSION_TIMEOUT` |
| `cpu` | `IP_KERNEL`, `PUERTO_KERNEL`, `IP_MEMORY`, `PUERTO_MEMORY`, `SEGMENT_MAX_SIZE`, `IP_MEMORY_STICK_0`, `PUERTO_MEMORY_STICK_0` |
| `io` | `KERNEL_SCHEDULER_IP`, `KERNEL_SCHEDULER_PORT` |
| `memory_stick` | `KERNEL_MEMORY_IP`, `KERNEL_MEMORY_PORT`, `MEMORY_STICK_PORT`, `MEMORY_DELAY` |
| `swap` | `KERNEL_MEMORY_IP`, `KERNEL_MEMORY_PORT`, `SWAP_FILE_PATH`, `SWAP_FILE_SIZE`, `BLOCK_SIZE` |

### Puertos del proyecto

| Módulo | Puerto | Quién se conecta |
|---|---|---|
| Kernel Memory | `23841` | KS, CPU, MS, Swap |
| Kernel Scheduler | `19337` | CPU, IO |
| Memory Stick | `27643` | CPU |

Estos puertos son altos y no redondos para minimizar conflictos en laboratorios ajenos.

---

## 5. Script de despliegue — deploy.sh

El archivo `deploy.sh` en la raíz del proyecto compila y levanta un único módulo. Es el método recomendado para arrancar el sistema, tanto en desarrollo local como en VMs distribuidas.

### Uso

```bash
./deploy.sh <módulo> [argumento_extra]
```

| Comando | Descripción |
|---|---|
| `./deploy.sh km` | Kernel Memory |
| `./deploy.sh swap` | Swap |
| `./deploy.sh ms 1024` | Memory Stick con buffer de 1024 bytes |
| `./deploy.sh ks /ruta/al/script.prc` | Kernel Scheduler (requiere ruta al script inicial) |
| `./deploy.sh cpu 0` | CPU con id=0 (entero único por instancia) |
| `./deploy.sh io STDOUT` | IO de tipo STDOUT (también: STDIN, SLEEP) |

El script:
1. Compila `utils` + el módulo elegido (a menos que `NO_BUILD=1` esté definido).
2. Verifica que el archivo de config exista.
3. Reemplaza el proceso del shell con el binario del módulo (`exec`).

### Saltar compilación (cuando ya se compiló)

```bash
NO_BUILD=1 ./deploy.sh km
```

---

## 6. Despliegue en una sola máquina (desarrollo)

Útil para pruebas locales. Todos los módulos corren en la misma máquina; todas las IPs son `127.0.0.1`.

### Configs para local (todos los módulos)

`kernel_memory/kernel_memory.config`:
```
LOG_LEVEL=INFO
KERNEL_MEMORY_PORT=23841
ALLOCATION_STRATEGY=BEST_FIT
SEGMENT_MAX_SIZE=128
INSTRUCTION_DELAY=0
COMPACTION_DELAY=0
SCRIPTS_BASEPATH=/home/utnso/pruebas
```

`kernel_scheduler/kernel_scheduler.config`:
```
LOG_LEVEL=INFO
KERNEL_MEMORY_IP=127.0.0.1
KERNEL_MEMORY_PORT=23841
KERNEL_SCHEDULER_PORT=19337
PLANIFICATION_ALGORITHM=FIFO
RR_QUANTUM=1500
SUSPENSION_TIMEOUT=35000
```

`cpu/cpu.config`:
```
LOG_LEVEL=INFO
IP_KERNEL=127.0.0.1
PUERTO_KERNEL=19337
IP_MEMORY=127.0.0.1
PUERTO_MEMORY=23841
SEGMENT_MAX_SIZE=128
IP_MEMORY_STICK_0=127.0.0.1
PUERTO_MEMORY_STICK_0=27643
```

`io/io.config`:
```
LOG_LEVEL=INFO
KERNEL_SCHEDULER_IP=127.0.0.1
KERNEL_SCHEDULER_PORT=19337
```

`memory_stick/memory_stick.config`:
```
LOG_LEVEL=INFO
KERNEL_MEMORY_IP=127.0.0.1
KERNEL_MEMORY_PORT=23841
MEMORY_STICK_PORT=27643
MEMORY_DELAY=0
```

`swap/swap.config`:
```
LOG_LEVEL=INFO
KERNEL_MEMORY_IP=127.0.0.1
KERNEL_MEMORY_PORT=23841
SWAP_FILE_PATH=/tmp/tp_swap.bin
SWAP_FILE_SIZE=65536
BLOCK_SIZE=256
```

### Levantar (cada comando en una terminal distinta)

```bash
./deploy.sh km
./deploy.sh swap
./deploy.sh ms 1024
./deploy.sh ks /ruta/al/script.prc
./deploy.sh cpu 0
./deploy.sh io STDOUT
./deploy.sh io STDIN
./deploy.sh io SLEEP
```

---

## 7. Despliegue distribuido en múltiples VMs

Esta sección explica paso a paso cómo desplegar el proyecto cuando cada módulo corre en una máquina virtual distinta. El procedimiento fue diseñado para alguien que no tiene experiencia previa con redes.

### 7.0 Preparar una VM base y clonarla (recomendado)

Instalar el sistema operativo y las dependencias 5 veces a mano es lento y propenso a errores. Conviene armar **una VM "base"** con todo instalado y **clonarla** 4 veces más. Esto ahorra tiempo, pero clonar una VM trae un problema que hay que resolver: **direcciones MAC duplicadas**.

#### Paso A — Armar la VM base

1. Crear una VM nueva (Lubuntu 22.04, igual que en evaluación) e instalar el sistema operativo.
2. Instalar las dependencias de la [Sección 2](#2-instalar-dependencias) (`build-essential`, `make`, `git`, `libreadline-dev`, `so-commons-library`).
3. **No es necesario** clonar el repositorio del TP en la VM base — mejor hacerlo en cada clon ya con su IP definitiva, así cada VM queda con su propia copia y no hay que preocuparse por sincronizarlas.
4. Apagar la VM (`sudo shutdown now`), no solo suspenderla. Clonar una VM que sigue "prendida" puede dejar el disco en un estado inconsistente.

#### Paso B — Clonar la VM

En VirtualBox: clic derecho sobre la VM base → **Clonar...**

- Elegir **"Clonación completa"** (no "vinculada"), para que cada VM sea un disco independiente y no dependa de la VM base para arrancar.
- **Importante:** en la pantalla de clonación hay un checkbox que dice algo como **"Reinicializar la dirección MAC de todas las tarjetas de red" / "Reinitialize the MAC address of all network cards"**. Dejarlo **tildado** (es la opción por defecto). Repetir el clonado 4 veces (una VM por cada módulo restante).

**¿Qué es la dirección MAC y por qué importa?**

Cada placa de red (física o virtual) tiene un identificador único de fábrica llamado dirección MAC (ej. `08:00:27:1a:2b:3c`), distinto de la IP. La red local lo usa para saber a qué máquina entregarle cada paquete, antes incluso de que exista una IP asignada.

Cuando VirtualBox clona una VM, por defecto **copia también la MAC** de las placas de red virtuales. Si dos VMs prendidas al mismo tiempo tienen la misma MAC, la red se confunde: puede que ninguna de las dos consiga IP por DHCP, que los `ping` fallen de forma intermitente, o que los paquetes le lleguen a la VM equivocada. Por eso hay que "regenerar" (asignarle una MAC nueva y distinta) a cada clon — el checkbox del Paso B hace esto automáticamente.

**¿Cómo verifico o corrijo la MAC después de clonar?**

Si ya clonaste sin tildar el checkbox, se puede regenerar después sin volver a clonar:

- En VirtualBox, con la VM apagada: **Configuración → Red → Avanzadas → Generar nueva dirección MAC** (ícono de flechas en círculo, al lado del campo MAC).
- Para confirmar que dos VMs tienen MACs distintas una vez prendidas, ejecutar en cada una:
  ```bash
  ip link show
  ```
  y comparar el campo `link/ether` de la interfaz activa (ej. `ens33`). Deben ser diferentes entre VMs.

**Otras dos cosas que conviene cambiar en cada clon (recomendado, no obligatorio):**

- **Hostname**, para no confundirse mirando la terminal de cuál VM es cuál:
  ```bash
  sudo hostnamectl set-hostname vm-kernel-memory   # ejemplo, uno distinto por VM
  ```
- **machine-id**, que Linux también copia al clonar y en algunos casos interfiere con el DHCP (la IP que asigna el router):
  ```bash
  sudo rm /etc/machine-id
  sudo systemd-machine-id-setup
  sudo reboot
  ```

Con la MAC regenerada, cada VM va a obtener su propia IP al reiniciar — usar esa IP en el Paso 1 de la sección 7.3 de más abajo.

### 7.1 Conceptos básicos de red (leer antes de continuar)

**¿Qué es una dirección IP?**
Una dirección IP (ej. `10.100.3.47`) es como el número de teléfono de una computadora en la red. Para que dos módulos se comuniquen, el módulo cliente necesita saber la IP de la máquina donde corre el módulo servidor.

**¿Por qué no usar `127.0.0.1`?**
`127.0.0.1` (también llamada `localhost`) significa "esta misma máquina". Si un módulo en la VM1 intenta conectarse a `127.0.0.1`, se conecta a sí mismo — no a la VM2. En un despliegue distribuido, hay que reemplazar `127.0.0.1` por la IP real de cada máquina.

**¿Cómo sé cuál es la IP de mi VM?**
Al iniciar sesión en cada VM, ejecutar:
```bash
ip addr show
```
Buscar la interfaz activa (generalmente `ens33`, `ens3`, o `eth0`). El campo `inet` muestra la IP:
```
2: ens33: <BROADCAST,MULTICAST,UP,LOWER_UP>
    inet 10.100.3.47/24 brd 10.100.3.255 scope global ens33
```
La IP es `10.100.3.47` (ignorar la parte `/24`).

Alternativa más corta:
```bash
hostname -I
```
Devuelve todas las IPs separadas por espacio. Tomar la primera.

**¿Cómo sé si dos VMs se pueden ver entre sí?**
Desde una VM, ejecutar:
```bash
ping <IP_de_la_otra_VM>
```
Si aparecen líneas como `64 bytes from 10.100.3.10: icmp_seq=1 ttl=64 time=0.4 ms`, la red funciona. Presionar `Ctrl+C` para detener.

Si `ping` falla con "Destination Host Unreachable" o no responde, el problema es de configuración de red en el hipervisor (ver nota al final de esta sección).

### 7.2 Asignación de módulos a VMs

Una distribución típica para el coloquio:

| VM | Módulo(s) |
|---|---|
| VM1 | Kernel Memory + Swap |
| VM2 | Kernel Scheduler |
| VM3 | CPU |
| VM4 | IO (STDOUT + STDIN + SLEEP) |
| VM5 | Memory Stick |

### 7.3 Paso a paso

#### Paso 1 — Obtener las IPs de todas las VMs

En **cada** VM, ejecutar `hostname -I` y anotar la IP. Completar esta tabla y compartirla con todos antes de continuar:

| VM | Módulo | IP |
|---|---|---|
| VM1 | Kernel Memory + Swap | `__________` |
| VM2 | Kernel Scheduler | `__________` |
| VM3 | CPU | `__________` |
| VM4 | IO | `__________` |
| VM5 | Memory Stick | `__________` |

#### Paso 2 — Verificar conectividad entre VMs

Desde VM2, verificar que puede llegar a VM1:
```bash
ping <IP_de_VM1>
```
Repetir para cada par de VMs que se van a comunicar. Si algún `ping` falla, ver la nota al final de esta sección.

#### Paso 3 — Instalar dependencias en cada VM

En **cada** VM ejecutar los comandos de la [Sección 2](#2-instalar-dependencias).

#### Paso 4 — Clonar el repositorio en cada VM

En **cada** VM:
```bash
git clone https://github.com/sisoputnfrba/tp-2026-1c-Impactante.git
cd tp-2026-1c-Impactante
```

#### Paso 5 — Editar los configs con las IPs reales

En cada VM, editar el config del módulo que va a correr en esa máquina. Reemplazar `127.0.0.1` por la IP de la VM donde corre el módulo destino.

Los ejemplos a continuación usan estas IPs de ejemplo (reemplazar con las reales):

| VM | IP de ejemplo |
|---|---|
| VM1 — Kernel Memory | `10.100.3.10` |
| VM2 — Kernel Scheduler | `10.100.3.11` |
| VM3 — CPU | `10.100.3.12` |
| VM4 — IO | `10.100.3.13` |
| VM5 — Memory Stick | `10.100.3.14` |

---

**VM1 — `kernel_memory/kernel_memory.config`:**
```
LOG_LEVEL=INFO
KERNEL_MEMORY_PORT=23841
ALLOCATION_STRATEGY=BEST_FIT
SEGMENT_MAX_SIZE=128
INSTRUCTION_DELAY=0
COMPACTION_DELAY=0
SCRIPTS_BASEPATH=/home/utnso/pruebas
```
> KM no necesita la IP de nadie — él escucha y los demás se conectan a él.

**VM1 — `swap/swap.config`:**
```
LOG_LEVEL=INFO
KERNEL_MEMORY_IP=127.0.0.1
KERNEL_MEMORY_PORT=23841
SWAP_FILE_PATH=/tmp/tp_swap.bin
SWAP_FILE_SIZE=65536
BLOCK_SIZE=256
```
> Swap corre en la misma VM que KM, por eso usa `127.0.0.1`.

---

**VM2 — `kernel_scheduler/kernel_scheduler.config`:**
```
LOG_LEVEL=INFO
KERNEL_MEMORY_IP=10.100.3.10
KERNEL_MEMORY_PORT=23841
KERNEL_SCHEDULER_PORT=19337
PLANIFICATION_ALGORITHM=FIFO
RR_QUANTUM=1500
SUSPENSION_TIMEOUT=35000
```
> `KERNEL_MEMORY_IP` = IP de VM1.

---

**VM3 — `cpu/cpu.config`:**
```
LOG_LEVEL=INFO
IP_KERNEL=10.100.3.11
PUERTO_KERNEL=19337
IP_MEMORY=10.100.3.10
PUERTO_MEMORY=23841
SEGMENT_MAX_SIZE=128
IP_MEMORY_STICK_0=10.100.3.14
PUERTO_MEMORY_STICK_0=27643
```
> `IP_KERNEL` = IP de VM2 (KS). `IP_MEMORY` = IP de VM1 (KM). `IP_MEMORY_STICK_0` = IP de VM5 (MS).

---

**VM4 — `io/io.config`:**
```
LOG_LEVEL=INFO
KERNEL_SCHEDULER_IP=10.100.3.11
KERNEL_SCHEDULER_PORT=19337
```
> `KERNEL_SCHEDULER_IP` = IP de VM2.

---

**VM5 — `memory_stick/memory_stick.config`:**
```
LOG_LEVEL=INFO
KERNEL_MEMORY_IP=10.100.3.10
KERNEL_MEMORY_PORT=23841
MEMORY_STICK_PORT=27643
MEMORY_DELAY=0
```
> `KERNEL_MEMORY_IP` = IP de VM1.

---

#### Paso 6 — Copiar los scripts de proceso a VM1

Los scripts `.prc` deben estar en la VM donde corre el Kernel Memory (VM1), en el path configurado en `SCRIPTS_BASEPATH`.

**Si los scripts ya están en VM1:**
```bash
mkdir -p /home/utnso/pruebas
cp scripts/*.prc /home/utnso/pruebas/
```

**Si los scripts están en otra VM y hay que copiarlos a VM1:**
```bash
# Ejecutar desde la VM que tiene los scripts:
scp scripts/*.prc utnso@10.100.3.10:/home/utnso/pruebas/
```
`scp` copia archivos por SSH. La sintaxis es `scp <archivo_local> <usuario>@<ip_destino>:<directorio_destino>`. Pedirá la contraseña del usuario en VM1.

> Si `scp` falla porque SSH no está instalado: `sudo apt install -y openssh-server openssh-client`

#### Paso 7 — Levantar los módulos en orden

Abrir una terminal por módulo. Ver el [Orden de inicio](#8-orden-de-inicio).

**VM1** (dos terminales):
```bash
./deploy.sh km        # terminal 1
./deploy.sh swap      # terminal 2
```

**VM5:**
```bash
./deploy.sh ms 1024
```

**VM2:**
```bash
./deploy.sh ks /home/utnso/pruebas/PLANI_PRE_0.prc
```

**VM4** (tres terminales):
```bash
./deploy.sh io SLEEP    # terminal 1
./deploy.sh io STDOUT   # terminal 2
./deploy.sh io STDIN    # terminal 3
```

**VM3** (última en levantar):
```bash
./deploy.sh cpu 0
```

#### Paso 8 — Verificar que todo conectó

Al levantar cada módulo, buscar en sus logs:
- KM: `Kernel Memory escuchando en puerto 23841`
- KS: `Conectado a Kernel Memory`
- MS: `Conectado a Kernel Memory`
- Swap: `Conectado a Kernel Memory`
- CPU: `Conectado a Kernel Scheduler` y `Conectado a Kernel Memory`
- IO: `Conectado a Kernel Scheduler`

Si un módulo no puede conectarse:
1. Verificar que el módulo destino esté levantado.
2. Probar `ping <ip_destino>` desde la VM con el problema.
3. Verificar que la IP en el `.config` sea la correcta.
4. Si hay firewall activo: `sudo ufw status` — si dice "active", intentar `sudo ufw disable` temporalmente.

> **Nota sobre configuración de red en el hipervisor:**
> Para que las VMs se vean entre sí, deben estar en la misma red virtual. En VirtualBox: el adaptador de red de cada VM debe estar en modo **Red interna** (todas con el mismo nombre de red) o en modo **Adaptador puente** (conectadas a la red física del laboratorio). El modo **NAT** (el predeterminado) no permite que las VMs se vean entre sí.

---

## 8. Orden de inicio

Los módulos deben levantarse en este orden porque cada uno espera que sus dependencias ya estén escuchando:

```
1. Kernel Memory     — no depende de nadie; todos los demás se conectan a él
2. Swap              — conecta a KM
3. Memory Stick      — conecta a KM
4. Kernel Scheduler  — conecta a KM; luego espera conexiones de CPU e IO
5. IO SLEEP          — conecta a KS
6. IO STDOUT         — conecta a KS
7. IO STDIN          — conecta a KS
8. CPU               — conecta a KS y KM (último en iniciar)
```

> Si un módulo falla con "connection refused", el módulo destino todavía no está escuchando. Esperar unos segundos y volver a levantar el que falló.

---

## 9. Scripts de proceso (.prc)

Los scripts de proceso contienen instrucciones que ejecuta la CPU, una por línea. El Kernel Scheduler recibe la ruta del script del proceso inicial como argumento al arrancar.

El Kernel Memory lee los scripts desde `SCRIPTS_BASEPATH`. Los scripts de procesos hijos (lanzados con `INIT_PROC`) también deben estar en ese directorio.

### Copiar los scripts antes de levantar el sistema

```bash
mkdir -p /home/utnso/pruebas
cp scripts/*.prc /home/utnso/pruebas/
```

Para las pruebas preliminares de la cátedra:

```bash
git clone https://github.com/sisoputnfrba/plug-n-pray-pruebas.git
cp plug-n-pray-pruebas/*.prc /home/utnso/pruebas/
```

### Instrucciones disponibles

`SET`, `MOV_IN`, `MOV_OUT`, `SUM`, `SUB`, `JNZ`, `SLEEP`, `STDOUT`, `STDIN`, `MEM_ALLOC`, `MEM_FREE`, `MUTEX_CREATE`, `MUTEX_LOCK`, `MUTEX_UNLOCK`, `INIT_PROC`, `EXIT`.

Ver la consigna del TP para el formato y semántica de cada instrucción.

---

## 10. Pruebas preliminares

Los scripts de prueba oficiales están en el repositorio `sisoputnfrba/plug-n-pray-pruebas`. Copiar los `.prc` al `SCRIPTS_BASEPATH` del Kernel Memory antes de correr cada suite.

```bash
git clone https://github.com/sisoputnfrba/plug-n-pray-pruebas.git
cp plug-n-pray-pruebas/*.prc /home/utnso/pruebas/
```

### 10.1 Planificación preliminar (PLANI_PRE)

**Objetivo:** validar la planificación de corto plazo sin involucrar memoria.

**Requisitos de configuración:**
- 1 sola CPU conectada.
- `SUSPENSION_TIMEOUT` alto (ej. `60000`) para no entrar en suspensión durante la prueba.
  Si se quiere verificar la suspensión, bajar el valor a menos de `20000`.

**Scripts:**

| Archivo | Descripción |
|---|---|
| `PLANI_PRE_0.prc` | Script maestro. Lanza los subprocesos con `INIT_PROC` y termina. |
| `PLANI_PRE_1.prc` | `SET` de registros + `SLEEP 20000` (dos veces) + EXIT. Activa suspensión si `SUSPENSION_TIMEOUT < 20000`. |
| `PLANI_PRE_2.prc` | Countdown con `SUB AX BX` + `JNZ AX` — loop con salto condicional. |
| `PLANI_PRE_3.prc` | SET de todos los registros + `SLEEP 100` (repetido) + EXIT. |

**Ejecución:**
```bash
./deploy.sh ks /home/utnso/pruebas/PLANI_PRE_0.prc
```

**Resultado esperado:** los procesos hijos se crean, ejecutan y terminan. En los logs del KS deben verse transiciones `NEW → READY → EXEC → EXIT` para cada uno. Si `SUSPENSION_TIMEOUT` es menor al SLEEP de `PLANI_PRE_1`, esos procesos deben pasar a `SUSP. BLOCK` y luego regresar a `SUSP. READY → READY`.

### 10.2 Memoria preliminar (MEMORIA_PRE)

**Objetivo:** validar la creación, escritura, lectura y eliminación de segmentos de memoria.

**Requisitos de configuración:**
- Al menos 1 Memory Stick con buffer de **256 bytes** o más.
- `SEGMENT_MAX_SIZE=128` en el Kernel Memory y en el CPU.
- IO STDOUT e IO STDIN activos (necesarios para `MEMORIA_PRE_2`).

**Scripts:**

| Archivo | Descripción |
|---|---|
| `MEMORIA_PRE_0.prc` | Script maestro. Lanza `MEMORIA_PRE_3`, espera 10 s, luego lanza `MEMORIA_PRE_1` y `MEMORIA_PRE_2`. |
| `MEMORIA_PRE_1.prc` | Alloc de 4 segmentos, escribe con `MOV_OUT`, libera 2, re-alloca 1 de 128 B, lee con `MOV_IN`. Verifica compactación. |
| `MEMORIA_PRE_2.prc` | Alloc de 1 segmento, lee por STDIN, escribe por STDOUT. |
| `MEMORIA_PRE_3.prc` | `MEM_ALLOC` + `MOV_OUT` fuera de rango → termina con **segmentation fault** (comportamiento esperado). |

**Ejecución:**
```bash
./deploy.sh ks /home/utnso/pruebas/MEMORIA_PRE_0.prc
```

**Resultado esperado:**
1. `MEMORIA_PRE_3` termina rápidamente con segfault (correcto).
2. Tras los 10 s del SLEEP, se crean `MEMORIA_PRE_1` y `MEMORIA_PRE_2`.
3. `MEMORIA_PRE_1` completa el ciclo alloc → write → free → realloc → read sin errores.
4. `MEMORIA_PRE_2` bloquea en STDIN esperando input; al recibirlo, lo escribe por STDOUT y termina.

---

## 11. Cambios introducidos en v1.1 del enunciado

La versión 1.1 del enunciado (publicada 08/06/2026) introduce aclaraciones y correcciones que impactan en CK3.

### 11.1 Syscalls deben liberar la CPU

Ante cualquier syscall, la CPU debe:
1. Incrementar el Program Counter (PC) en 1.
2. Guardar el contexto en el Kernel Memory.
3. Enviar el PID al Kernel Scheduler (`MSG_DEVOLVER_PROCESO` con motivo `SYSCALL`).
4. Quedar libre para ejecutar otro proceso.

El KS decide cuándo redespachar el proceso.

**Impacto:** `cpu/src/cpu_syscalls.c` — las funciones para MUTEX_CREATE, MUTEX_UNLOCK y EXIT no deben esperar `MSG_OK` del KS. El KS redespacha al proceso vía el planificador normal.

### 11.2 MUTEX_LOCK bloqueante: el bloqueo pasa al KS

Con el cambio anterior, el bloqueo por `MUTEX_LOCK` cuando el mutex está tomado es responsabilidad exclusiva del KS:
- Mutex libre: KS asigna el mutex y redespacha inmediatamente.
- Mutex tomado: KS mueve el proceso a `BLOCK`. Al hacer `MUTEX_UNLOCK`, mueve de `BLOCK` a `READY`.

### 11.3 STDIN y STDOUT usan dirección física, no lógica

La CPU envía al KS una **dirección física** (ya traducida por la MMU) en lugar de lógica.

| | v1.0 | v1.1 |
|---|---|---|
| CPU → KS (STDOUT) | `{ pid, dir_logica, tamanio }` | `{ pid, dir_fisica, tamanio }` |
| CPU → KS (STDIN) | `{ pid, dir_logica, tamanio }` | `{ pid, dir_fisica, tamanio }` |

### 11.4 Flujo completo de STDOUT y STDIN con Kernel Memory

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

### 11.5 MEM_ALLOC/MEM_FREE: reenviar a la misma CPU

Las syscalls de memoria (`MEM_ALLOC`, `MEM_FREE`) deben reenviarse a la **misma CPU** que hizo la llamada, no a cualquier CPU libre. Si hay espacio no contiguo, el KM dispara compactación antes de asignar.

### 11.6 Orden FIFO en el desbloqueo de Mutex

Al liberar un mutex con procesos en espera, los procesos se desbloquean en el orden en que solicitaron el mutex (FIFO).

### 11.7 Des-suspensión usa algoritmo de búsqueda de huecos

Al des-suspender un proceso (restaurar segmentos de SWAP a memoria), el KM debe usar el algoritmo configurado (BEST FIT o WORST FIT) para ubicar los segmentos.

### 11.8 Direcciones físicas de Memory Sticks son globales

Las direcciones físicas son globales a todo el sistema. El KM lleva registro de qué rango cubre cada MS (ej. MS1: 0–255, MS2: 256–511). Si una operación cruza la frontera entre dos sticks, el KM la divide.

### 11.9 IO tiene 1 solo hilo de ejecución

Cada módulo IO opera con un único hilo y atiende pedidos del KS de forma secuencial.
