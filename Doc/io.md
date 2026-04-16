# IO — Guía de uso para el equipo

**Módulo:** `io/`
**Responsable:** Nicolas
**Estado:** Check 1 — conexión al Kernel Scheduler implementada

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

## Qué hace al arrancar (Check 1)

1. Valida los argumentos (`[Config]` y `[Tipo]`)
2. Crea el logger — genera `io_SLEEP.log`, `io_STDIN.log` o `io_STDOUT.log` según el tipo
3. Lee el archivo de configuración
4. Se conecta al Kernel Scheduler por TCP
5. Se identifica enviando su tipo con `MSG_IO_IDENTIFICACION`
6. Imprime el log obligatorio: `## Conectado a Kernel Scheduler`

---

## Logs generados

Los logs se guardan en el directorio desde donde se corra el binario:

```
io_SLEEP.log
io_STDIN.log
io_STDOUT.log
```

**Log obligatorio de la consigna** (debe aparecer exactamente así):
```
## Conectado a Kernel Scheduler
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

## Protocolo: mensaje de identificación

Al conectarse, IO envía un mensaje con:

| Campo | Valor |
|---|---|
| `op_code` | `MSG_IO_IDENTIFICACION` |
| `payload` | El tipo como string (`"STDIN"`, `"STDOUT"` o `"SLEEP"`) |

El Kernel Scheduler recibe este mensaje y sabe qué tipo de IO se conectó. Ver `utils/src/utils/protocolo.h` para los códigos de operación.

---

## Actualizaciones previstas

| Check | Qué se va a agregar |
|---|---|
| Check 2 | Loop de recepción de pedidos, implementación de SLEEP/STDIN/STDOUT |
| Check 3 | Sin cambios previstos en IO |
