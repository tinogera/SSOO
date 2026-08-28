# Plug & Pray

**Because Hot-Plugging is an Act of Faith**

Trabajo Práctico de la materia **Sistemas Operativos — UTN FRBA** (1C 2026).
Equipo **Impactante**.

Este proyecto simula, de forma distribuida, el funcionamiento de un sistema
operativo: planificación de procesos, administración de memoria con
segmentación, swap, entrada/salida y una CPU con su propio ciclo de
instrucción. Cada una de esas responsabilidades vive en un módulo
independiente, y los módulos se comunican entre sí por sockets TCP, tal como
lo harían procesos en máquinas distintas.

> Si es la primera vez que abrís este repo, andá directo a
> [Cómo levantar el proyecto en 5 minutos](#cómo-levantar-el-proyecto-en-5-minutos).

---

## Índice

1. [¿Qué hace el proyecto?](#qué-hace-el-proyecto)
2. [Arquitectura y módulos](#arquitectura-y-módulos)
3. [Estructura del repositorio](#estructura-del-repositorio)
4. [Requisitos previos](#requisitos-previos)
5. [Cómo levantar el proyecto en 5 minutos](#cómo-levantar-el-proyecto-en-5-minutos)
6. [Compilación módulo por módulo](#compilación-módulo-por-módulo)
7. [Configuración](#configuración)
8. [Scripts de proceso (.prc)](#scripts-de-proceso-prc)
9. [Tests](#tests)
10. [Documentación completa](#documentación-completa)
11. [Equipo](#equipo)

---

## ¿Qué hace el proyecto?

El sistema ejecuta "procesos" definidos como scripts de instrucciones
(`SET`, `SUM`, `MOV_IN`, `MEM_ALLOC`, `MUTEX_LOCK`, `SLEEP`, `STDOUT`, `EXIT`,
etc.). Una CPU los va buscando y ejecutando de a una instrucción por vez,
consultando memoria y reportando syscalls a un kernel que decide cuándo cada
proceso corre, se bloquea o se suspende. Es, en chico, lo mismo que hace un
sistema operativo real, pero repartido en varios programas en C que hablan
entre sí por red.

## Arquitectura y módulos

```
                         ┌───────────────────┐
                         │   Kernel Memory    │  memoria de procesos,
                         │        (KM)        │  segmentación, swap
                         └─────────┬──────────┘
                                   │
              ┌────────────────────┼────────────────────┐
              │                    │                     │
     ┌────────┴────────┐  ┌────────┴────────┐   ┌────────┴────────┐
     │ Kernel Scheduler │  │   Memory Stick   │   │       Swap      │
     │       (KS)       │  │       (MS)       │   │                 │
     └────────┬─────────┘  └─────────────────┘   └─────────────────┘
              │
      ┌───────┴───────┐
      │               │
 ┌────┴────┐     ┌────┴────┐
 │   CPU   │     │   IO    │  (STDOUT / STDIN / SLEEP)
 └─────────┘     └─────────┘
```

| Módulo | Carpeta | Responsabilidad |
|---|---|---|
| **Kernel Scheduler (KS)** | [`kernel_scheduler/`](kernel_scheduler) | Planificación de procesos (largo, mediano y corto plazo), colas de estados, mutex, algoritmos FIFO/RR |
| **Kernel Memory (KM)** | [`kernel_memory/`](kernel_memory) | Memoria de instrucciones y de datos, segmentación, algoritmos de asignación (BEST/WORST FIT), compactación, swap de procesos suspendidos |
| **CPU** | [`cpu/`](cpu) | Ciclo de instrucción (fetch → decode → execute → check interrupt), registros, MMU, traducción de direcciones lógicas a físicas |
| **Memory Stick (MS)** | [`memory_stick/`](memory_stick) | Simula un módulo físico de memoria; lee y escribe bytes a pedido del Kernel Memory |
| **Swap** | [`swap/`](swap) | Persiste en disco los segmentos de procesos suspendidos |
| **IO** | [`io/`](io) | Simula dispositivos de entrada/salida: `STDOUT`, `STDIN` y `SLEEP` |
| **Utils** | [`utils/`](utils) | Biblioteca estática compartida por todos los módulos (sockets, serialización, helpers comunes) |

Todos los módulos son binarios de C independientes que se compilan por
separado y se conectan entre sí por TCP usando la
[so-commons-library](https://github.com/sisoputnfrba/so-commons-library) de
la cátedra.

## Estructura del repositorio

```
.
├── cpu/                  # Módulo CPU (código, tests, mock de KM para pruebas locales)
├── io/                   # Módulo IO
├── kernel_memory/        # Módulo Kernel Memory
├── kernel_scheduler/     # Módulo Kernel Scheduler
├── memory_stick/         # Módulo Memory Stick
├── swap/                 # Módulo Swap
├── utils/                # Biblioteca compartida (sockets, serialización)
├── scripts/              # Scripts de proceso (.txt) de prueba
├── configs/              # Perfiles de configuración alternativos (ver deploy.sh -P)
├── Doc/                  # Documentación del TP: consigna, instalación, planificación del equipo
├── deploy.sh             # Compila y levanta un módulo
├── set_config.sh         # Edita una clave de un .config sin abrir un editor
├── set_ip.sh             # Actualiza una IP en todos los .config que la referencian
└── tp.code-workspace     # Workspace de VS Code con todos los módulos
```

Cada módulo tiene la misma estructura interna:

```
<modulo>/
├── src/                       # Código fuente (.c / .h)
├── tests/                     # Tests unitarios (framework cspecs)
├── vendor/cspecs/              # Framework de testing de la cátedra
├── Makefile / settings.mk      # Build
├── <modulo>.config             # Configuración local (no se versiona, ver más abajo)
└── <modulo>.config.example     # Ejemplo comentado de cada clave de configuración
```

## Requisitos previos

- **Sistema operativo:** Linux (Ubuntu/Lubuntu 22.04 recomendado). También
  funciona en WSL2.
- **Herramientas de compilación:**

  ```bash
  sudo apt install -y build-essential make git libreadline-dev
  ```

- **[so-commons-library](https://github.com/sisoputnfrba/so-commons-library)**,
  la biblioteca de la cátedra (no está en APT, se instala manualmente):

  ```bash
  git clone https://github.com/sisoputnfrba/so-commons-library.git
  cd so-commons-library
  make install
  cd ..
  rm -rf so-commons-library
  ```

  Esto instala los headers en `/usr/include/commons/` y la biblioteca en
  `/usr/lib/libcommons.so`. Si ya está instalada no hace falta repetir el
  paso (`ls /usr/lib/libcommons.so` no debería dar error).

## Cómo levantar el proyecto en 5 minutos

Pensado para correr **todo en una sola máquina**, con fines de desarrollo o
prueba rápida (todas las IPs son `127.0.0.1`, que ya vienen configuradas por
defecto en los `.config` del repo).

1. Cloná el repo e instalá las dependencias de la sección anterior.
2. Copiá algún script de proceso de prueba al lugar donde el Kernel Memory
   los va a buscar (por defecto `/home/utnso/pruebas`, configurable con
   `SCRIPTS_BASEPATH` en `kernel_memory/kernel_memory.config`):

   ```bash
   mkdir -p /home/utnso/pruebas
   cp scripts/*.txt /home/utnso/pruebas/
   ```

3. Abrí **una terminal por módulo** y levantá todo en este orden (cada uno
   espera que el anterior ya esté escuchando):

   ```bash
   ./deploy.sh km                 # Kernel Memory
   ./deploy.sh swap                # Swap
   ./deploy.sh ms 1024              # Memory Stick, con 1024 bytes de buffer
   ./deploy.sh ks scripts/0.txt      # Kernel Scheduler + proceso inicial
   ./deploy.sh io STDOUT             # IO de salida
   ./deploy.sh io STDIN              # IO de entrada
   ./deploy.sh io SLEEP              # IO de sleep
   ./deploy.sh cpu 0                  # CPU con id=0 (levantarla última)
   ```

   `deploy.sh` compila el módulo (y `utils`) automáticamente antes de
   levantarlo. Si ya compilaste y solo querés reiniciar el proceso, saltealo
   con `NO_BUILD=1 ./deploy.sh <modulo>`.

4. Mirá los logs de cada terminal: cada módulo indica cuándo se conecta
   correctamente a sus dependencias (ej. `Conectado a Kernel Memory`).

Para desplegar en **varias máquinas virtuales** (como en el coloquio), la
guía completa paso a paso — incluyendo cómo clonar VMs sin pisar IPs/MACs,
cómo editar cada `.config` y en qué orden levantar todo — está en
[`Doc/instalacion.md`](Doc/instalacion.md).

## Compilación módulo por módulo

Si no querés usar `deploy.sh` y preferís compilar a mano, cada módulo se
compila de forma independiente con `make` desde su propia carpeta. El
binario resultante queda en `bin/` dentro del módulo:

```bash
cd kernel_memory
make
./bin/kernel_memory kernel_memory.config
```

Comandos de `make` disponibles en cada módulo:

| Comando | Qué hace |
|---|---|
| `make` / `make debug` | Compila con símbolos de debug (`-g -Wall`) |
| `make release` | Compila optimizado (`-O3`) |
| `make test` | Compila los tests unitarios del módulo (ver [Tests](#tests)) |
| `make clean` | Borra binarios y objetos generados |

`utils` es una excepción: no genera un ejecutable sino una biblioteca
estática (`lib/libutils.a`) que consumen todos los demás módulos, así que no
hace falta correrla, solo compilarla (`deploy.sh` ya lo hace automáticamente
antes de compilar cualquier otro módulo).

## Configuración

Cada módulo lee su configuración de un archivo `<modulo>.config` en su
propia carpeta (por ejemplo `cpu/cpu.config`). Esos archivos **ya vienen en
el repo** con valores por defecto para correr todo en `127.0.0.1`, así que
no hace falta crearlos: alcanza con editarlos si vas a correr el proyecto en
una red distribuida (varias VMs con IPs distintas).

Cada `.config` tiene al lado un `.config.example` con cada clave documentada
— sirve de referencia rápida aunque edites a mano.

Para no tener que abrir un editor, el repo trae dos scripts:

```bash
# Cambiar/agregar una clave puntual en el config de un módulo
./set_config.sh <modulo> CLAVE=VALOR [CLAVE=VALOR ...]
./set_config.sh km KERNEL_MEMORY_PORT=23841 SCRIPTS_BASEPATH=/home/utnso/pruebas

# Actualizar la IP de un rol (KM, KS o un Memory Stick) en TODOS los .config
# que la referencian, de una sola vez
./set_ip.sh km <ip>
./set_ip.sh ks <ip>
./set_ip.sh ms <ip> [indice]
```

`<modulo>` usa siempre los mismos nombres cortos que `deploy.sh`: `km`, `ks`,
`cpu`, `io`, `ms`, `swap`.

> **Ojo:** el `.gitignore` tiene una regla para `*.config` pensada para que
> cada integrante mantenga su propia config sin pisar la de sus compañeros,
> pero los `.config` de este repo ya estaban commiteados antes de esa regla
> — si los editás y hacés `git add`/`git commit`, el cambio **sí** se sube.
> Revisá `git status` antes de commitear, o descartá la edición local con
> `git checkout -- <archivo>.config` cuando ya no la necesites.

Detalle de cada clave por módulo, tabla de puertos usados y ejemplos
completos de configuración (local y distribuida) en
[`Doc/instalacion.md`, sección 4](Doc/instalacion.md#4-configurar-cada-módulo).

## Scripts de proceso (.prc)

Los "programas" que ejecuta la CPU son archivos de texto plano, una
instrucción por línea. Hay ejemplos en [`scripts/`](scripts). El Kernel
Memory los busca en `SCRIPTS_BASEPATH/<PID>.txt`.

Instrucciones soportadas: `SET`, `MOV_IN`, `MOV_OUT`, `SUM`, `SUB`, `JNZ`,
`SLEEP`, `STDOUT`, `STDIN`, `MEM_ALLOC`, `MEM_FREE`, `MUTEX_CREATE`,
`MUTEX_LOCK`, `MUTEX_UNLOCK`, `INIT_PROC`, `EXIT`. El significado exacto de
cada una está en la consigna del TP ([`Doc/Plug & Pray.md`](<Doc/Plug & Pray.md>))
y en [`scripts/README.md`](scripts/README.md).

## Tests

Los tests unitarios usan [cspecs](https://github.com/sisoputnfrba/cspecs), el
framework de testing de la cátedra, incluido como submódulo/vendor dentro de
cada módulo (`<modulo>/vendor/cspecs`) y escritos en `<modulo>/tests/`.

Para correr los tests de un módulo:

```bash
cd cpu
make test
./bin/cpu_tests
```

Repetí el mismo procedimiento (`make test` + ejecutar el binario `_tests`)
en cualquier otro módulo que tenga carpeta `tests/` (`kernel_scheduler`,
`io`, `utils`).

El módulo `cpu` además incluye un mock de Kernel Memory
([`cpu/mocks/`](cpu/mocks)) para poder probar la CPU de punta a punta sin
tener que levantar el Kernel Memory real — instrucciones de uso en
[`cpu/mocks/README.md`](cpu/mocks/README.md).

## Documentación completa

- [`Doc/instalacion.md`](Doc/instalacion.md) — guía extendida de instalación,
  despliegue local y distribuido (multi-VM), orden de inicio de los módulos y
  pruebas preliminares de la cátedra (planificación y memoria), pensada para
  quien no tiene experiencia previa con redes.
- [`Doc/Plug & Pray.md`](<Doc/Plug & Pray.md>) y
  [`Doc/Plug & Pray v1.1.md`](<Doc/Plug & Pray v1.1.md>) — consigna oficial
  del TP (arquitectura, protocolo, formato de mensajes, logs obligatorios).
- [`Doc/io.md`](Doc/io.md) / [`Doc/utils.md`](Doc/utils.md) — notas técnicas
  puntuales sobre esos módulos.
- [`Doc/planificacion/`](Doc/planificacion) — organización interna del
  equipo: checkpoints, asignación de módulos por integrante y estrategia de
  branching.

## Equipo

**Impactante** — Sistemas Operativos, UTN FRBA, 1C 2026:

- Kevin Luciano Castillo Panta
- Santiago Gerardi
- Luciano Lisachi
- Juan Manuel Fernandez Vazquez
- Nicolas Alessandro Barreiro
