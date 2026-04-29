# IO — Guía de uso para el equipo

**Módulo:** `io/`
**Responsable:** Nicolas
**Estado:** Check 2 en curso — SLEEP implementado, STDOUT/STDIN pendientes

---

## ¿Qué hace IO?

IO es el módulo que ejecuta las operaciones de entrada/salida en nombre de los procesos simulados. Se conecta al Kernel Scheduler al arrancar, se identifica con su tipo, y espera pedidos.

Hay tres tipos de IO, cada uno corre como un proceso separado:

| Tipo | Descripción |
|---|---|
| `SLEEP` | Recibe un tiempo en ms y espera ese tiempo (`usleep`) |
| `STDIN` | Lee caracteres del teclado y los envía al Kernel Scheduler para escribir en memoria |
| `STDOUT` | Recibe bytes del Kernel Scheduler y los imprime por pantalla |

---

## Cómo arrancar el módulo

```bash
./bin/io [Archivo Config] [Tipo]
```

**Ejemplos:**
```bash
./bin/io io.config SLEEP
./bin/io io.config STDIN
./bin/io io.config STDOUT
```

El Kernel Scheduler tiene que estar corriendo antes de iniciar IO.

---

## Archivo de configuración

Crear `io.config` en el directorio desde donde se corra el binario. Este archivo es **local**, no se commitea.

```
LOG_LEVEL=INFO
KERNEL_SCHEDULER_IP=127.0.0.1
KERNEL_SCHEDULER_PORT=37214
```

| Campo | Descripción |
|---|---|
| `LOG_LEVEL` | Nivel de log: `INFO`, `DEBUG`, `WARNING`, `ERROR` |
| `KERNEL_SCHEDULER_IP` | IP de la máquina donde corre el Kernel Scheduler |
| `KERNEL_SCHEDULER_PORT` | Puerto acordado con el equipo: `37214` |

> Para pruebas locales usar `127.0.0.1`. En el entorno distribuido (evaluación) cambiar la IP por la de la VM del Kernel Scheduler.

---

## Compilación

Compilar siempre utils primero:

```bash
cd utils && make && cd ..
cd io && make
```

---

## Qué hace al arrancar

1. Valida los argumentos (`[Config]` y `[Tipo]`)
2. Crea el logger — genera `io_SLEEP.log`, `io_STDIN.log` o `io_STDOUT.log` según el tipo
3. Lee el archivo de configuración
4. Se conecta al Kernel Scheduler por TCP
5. Se identifica enviando su tipo con `MSG_IO_IDENTIFICACION`
6. Imprime el log obligatorio: `## Conectado a Kernel Scheduler`
7. Entra en el loop de atención — espera pedidos del Kernel Scheduler

---

## Comportamiento por tipo (Check 2)

### SLEEP

Recibe `MSG_IO_SLEEP` con `{ pid, tiempo_ms }`, espera el tiempo indicado y notifica al KS con `MSG_IO_FIN`.

```
Flujo: KS → MSG_IO_SLEEP → IO → usleep(tiempo_ms * 1000) → MSG_IO_FIN → KS
```

Logs obligatorios:
```
## PID: <PID> - Inicio de IO
## PID: <PID> - Haciendo sleep por <TIEMPO> milisegundos
## PID: <PID> - Fin de IO
```

### STDOUT (pendiente — issue #24)

Recibe `MSG_IO_STDOUT` con `{ pid, contenido[] }`, imprime el contenido por pantalla y notifica al KS.

Logs obligatorios:
```
## PID: <PID> - Inicio de IO
## PID: <PID> - <CONTENIDO A IMPRIMIR>
## PID: <PID> - Fin de IO
```

### STDIN (pendiente — issue #25)

Recibe `MSG_IO_STDIN` con `{ pid, n_bytes }`, lee del teclado, trunca o rellena con `\0` según la longitud, y envía los datos al KS con `MSG_IO_STDIN_DATOS`.

Logs obligatorios:
```
## PID: <PID> - Inicio de IO
## PID: <PID> - Ingrese <N> caracteres:
## PID: <PID> - Fin de IO
```

---

## Logs generados

Los logs se guardan en el directorio desde donde se corra el binario:

```
io_SLEEP.log
io_STDIN.log
io_STDOUT.log
```

**Logs obligatorios de la consigna** (deben aparecer exactamente así):
```
## Conectado a Kernel Scheduler
## PID: <PID> - Inicio de IO
## PID: <PID> - Haciendo sleep por <TIEMPO> milisegundos
## PID: <PID> - Fin de IO
## PID: <PID> - <CONTENIDO A IMPRIMIR>      (STDOUT)
## PID: <PID> - Ingrese <N> caracteres:     (STDIN)
```

---

## Probar sin Kernel Scheduler (netcat)

Para verificar que IO conecta y envía la identificación sin tener el Kernel Scheduler real:

**Terminal 1 — servidor falso:**
```bash
nc -l 37214
```

**Terminal 2 — correr IO:**
```bash
./bin/io io.config SLEEP
```

En la Terminal 2 debe aparecer:
```
[INFO] ... IO: ## Conectado a Kernel Scheduler
```

En la Terminal 1 van a llegar bytes — el mensaje de identificación serializado.

---

## Protocolo: mensajes

Ver `utils/src/utils/protocolo.h` para los valores numéricos de cada op_code. Todos los mensajes usan el formato:

```
[ op_code (4B) | payload_size (4B) | payload (variable) ]
```

| Mensaje | Dirección | Payload |
|---|---|---|
| `MSG_IO_IDENTIFICACION` | IO → KS | string tipo (`"SLEEP"`, `"STDOUT"`, `"STDIN"`) |
| `MSG_IO_SLEEP` | KS → IO | `{ uint32_t pid, uint32_t tiempo_ms }` |
| `MSG_IO_STDOUT` | KS → IO | `{ uint32_t pid, char contenido[] }` |
| `MSG_IO_STDIN` | KS → IO | `{ uint32_t pid, uint32_t n_bytes }` |
| `MSG_IO_FIN` | IO → KS | `{ uint32_t pid }` |
| `MSG_IO_STDIN_DATOS` | IO → KS | `{ uint32_t pid, uint32_t n_bytes, uint8_t datos[] }` |

---

## Estado de implementación

| Funcionalidad | Estado |
|---|---|
| Conexión al KS e identificación | ✅ Check 1 |
| Loop de atención en `main.c` | ✅ Check 2 |
| IO tipo SLEEP | ✅ Check 2 (issue #23) |
| IO tipo STDOUT | ⬜ Pendiente (issue #24) |
| IO tipo STDIN | ⬜ Pendiente (issue #25) |
