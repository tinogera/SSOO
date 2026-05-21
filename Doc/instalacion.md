# Requisitos de Instalación — Plug & Pray

**Sistemas Operativos — UTN FRBA — Equipo Impactante**

---

## Sistema Operativo

| Entorno | SO | Uso |
|---|---|---|
| Desarrollo | Xubuntu 22.04 LTS (64-bit) | Máquinas de los integrantes del equipo |
| Evaluación | Ubuntu Server 22.04 LTS (64-bit) | VMs de la cátedra durante el coloquio |

Los pasos de instalación son idénticos en ambos entornos. La única diferencia es que Ubuntu Server **no tiene entorno gráfico**, por lo que todo se opera por terminal.

---

## Herramientas de compilación

```bash
sudo apt install -y build-essential make git
```

| Paquete | Descripción |
|---|---|
| `build-essential` | GCC, G++ y utilidades de compilación |
| `make` | Sistema de build usado por todos los módulos |
| `git` | Control de versiones |

---

## Dependencias de bibliotecas

Todos los módulos (`utils`, `cpu`, `io`, `kernel_scheduler`, `kernel_memory`, `memory_stick`, `swap`) enlazan contra las siguientes bibliotecas:

| Biblioteca | Paquete APT | Descripción |
|---|---|---|
| `libcommons` | instalación manual (ver abajo) | Biblioteca de la cátedra: logs, listas, configs |
| `libpthread` | incluida en `build-essential` | Threads POSIX |
| `libreadline` | `libreadline-dev` | Lectura de línea con historial (usada por IO STDIN) |
| `libm` | incluida en `build-essential` | Biblioteca matemática estándar de C |

```bash
sudo apt install -y libreadline-dev
```

---

## so-commons-library (biblioteca de la cátedra)

La biblioteca `so-commons-library` **no está en APT**; se instala manualmente desde el repositorio de la cátedra.

```bash
git clone https://github.com/sisoputnfrba/so-commons-library.git
cd so-commons-library
make install
```

Esto instala:
- Headers en `/usr/include/commons/`
- Biblioteca compartida en `/usr/lib/libcommons.so`

> Si ya está instalada (`ls /usr/lib/libcommons.so` devuelve el archivo), no es necesario reinstalar.

---

## Resumen: instalación completa en una máquina limpia

```bash
# 1. Herramientas de compilación y git
sudo apt install -y build-essential make git

# 2. Dependencias de desarrollo
sudo apt install -y libreadline-dev

# 3. so-commons-library (biblioteca de la cátedra)
git clone https://github.com/sisoputnfrba/so-commons-library.git
cd so-commons-library
make install
cd ..
rm -rf so-commons-library   # opcional: limpiar directorio de instalación

# 4. Clonar el repositorio del TP
git clone <URL_DEL_REPO>
cd tp-2026-1c-Impactante
```

---

## Compilación del proyecto

Una vez instaladas las dependencias, compilar en orden (utils primero, ya que es dependencia de todos):

```bash
cd utils && make && cd ..
cd cpu && make && cd ..
cd io && make && cd ..
cd kernel_scheduler && make && cd ..
cd kernel_memory && make && cd ..
cd memory_stick && make && cd ..
cd swap && make && cd ..
```

> `utils` genera `utils/lib/libutils.a`, que es enlazada estáticamente por todos los demás módulos.

---

## Verificación de instalación

```bash
# Verificar que libcommons está instalada
ls /usr/lib/libcommons.so

# Verificar que libreadline-dev está instalada (debe aparecer libreadline.so sin versión)
ls /usr/lib/x86_64-linux-gnu/libreadline.so

# Verificar gcc y make
gcc --version
make --version
```

---

## Configuración

Cada módulo trae un archivo `*.config.example` con todas las claves disponibles y sus valores por defecto. Antes de ejecutar, copiar y editar:

```bash
cp kernel_memory/kernel_memory.config.example  kernel_memory/kernel_memory.config
cp kernel_scheduler/kernel_scheduler.config.example kernel_scheduler/kernel_scheduler.config
cp cpu/cpu.config.example   cpu/cpu.config
cp io/io.config.example     io/io.config
cp memory_stick/memory_stick.config.example memory_stick/memory_stick.config
cp swap/swap.config.example swap/swap.config
```

Los archivos `.config` **no se commitean** (están en `.gitignore`). Cada integrante mantiene el suyo local con las IPs y puertos del entorno donde está corriendo.

---

## Notas

- Las pruebas de integración se corren en **entorno distribuido** (múltiples máquinas). Cada máquina debe cumplir estos requisitos.
- **Orden de inicio obligatorio:** Kernel Memory → Swap → Memory Stick(s) → Kernel Scheduler → IO(s) → CPU(s). Kernel Memory debe estar levantado antes que cualquier otro módulo porque todos se conectan a él al arrancar.
- Para el procedimiento completo de despliegue en VMs (incluyendo cómo descubrir la IP de cada máquina del laboratorio), ver `instalacion.md` en la raíz del repositorio.
