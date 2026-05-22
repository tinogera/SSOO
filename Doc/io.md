# IO — Guía de uso para el equipo

**Módulo:** `io/`
**Responsable:** Nicolas
**Estado:** Check 2 completo — SLEEP, STDOUT y STDIN implementados

---

## ¿Qué hace IO?

IO es el módulo que ejecuta las operaciones de entrada/salida en nombre de los procesos simulados. Corre como un proceso independiente, se conecta al Kernel Scheduler al arrancar, se identifica con su tipo, y espera pedidos en un loop.

Hay tres tipos. Cada tipo corre como un proceso separado:

| Tipo | Descripción |
|---|---|
| `SLEEP` | Recibe un tiempo en ms, espera ese tiempo y notifica al KS cuando termina |
| `STDOUT` | Recibe un string del KS y lo imprime por pantalla |
| `STDIN` | Pide al usuario que escriba por teclado y envía esos bytes al KS |

---

## Compilación

Compilar siempre utils primero (es dependencia estática de IO):

```bash
cd utils && make && cd ..
cd io && make
```

El binario queda en `io/bin/io`.

---

## Archivo de configuración

Crear `io.config` en el directorio desde donde se corra el binario. **No se commitea.**

```
LOG_LEVEL=INFO
KERNEL_SCHEDULER_IP=127.0.0.1
KERNEL_SCHEDULER_PORT=37214
```

| Campo | Descripción |
|---|---|
| `LOG_LEVEL` | `INFO`, `DEBUG`, `WARNING` o `ERROR` |
| `KERNEL_SCHEDULER_IP` | IP de la máquina donde corre el KS |
| `KERNEL_SCHEDULER_PORT` | Puerto donde el KS acepta conexiones de IO |

Para pruebas locales usar `127.0.0.1`. En el entorno distribuido (evaluación) poner la IP de la VM del KS.

---

## Cómo arrancar

```bash
./bin/io [Archivo Config] [Tipo]
```

```bash
# Ejemplos — cada uno en una terminal separada
./bin/io io.config SLEEP
./bin/io io.config STDOUT
./bin/io io.config STDIN
```

**Requisito:** el Kernel Scheduler tiene que estar escuchando antes de que IO arranque. IO hace `connect()` al inicio y falla si el KS no está disponible.

---

## Qué hace IO al arrancar

1. Valida los argumentos y que el tipo sea `SLEEP`, `STDOUT` o `STDIN`
2. Crea el logger → genera `io_SLEEP.log`, `io_STDOUT.log` o `io_STDIN.log`
3. Lee el archivo de configuración
4. Se conecta al KS por TCP
5. Se identifica enviando `MSG_IO_IDENTIFICACION` con su tipo como string
6. Imprime el log obligatorio: `## Conectado a Kernel Scheduler`
7. Entra en el loop de atención — espera mensajes del KS indefinidamente

Si el KS cierra la conexión, IO loguea un warning y termina limpiamente.

---

## Protocolo completo: mensajes y formato

Todos los mensajes siguen el formato de `utils`:

```
[ op_code (4B, big-endian) | payload_size (4B, big-endian) | payload (variable) ]
```

Usar siempre `enviar_mensaje()` y `recibir_mensaje()` de `utils/sockets.h` — manejan la serialización automáticamente.

---

## IO tipo SLEEP

### Flujo

```
KS  ──MSG_IO_SLEEP──►  IO  (duerme tiempo_ms ms)  ──MSG_IO_FIN──►  KS
```

### Cómo enviar el pedido desde el KS

Payload: struct `t_payload_io_sleep` definida en `protocolo.h`.

```c
#include <utils/protocolo.h>
#include <utils/sockets.h>

// fd_io = file descriptor del socket conectado al proceso IO SLEEP

t_payload_io_sleep pedido = {
    .pid       = pid_del_proceso,
    .tiempo_ms = 2000,           // 2 segundos
};

enviar_mensaje(fd_io, MSG_IO_SLEEP, &pedido, sizeof(pedido));
```

### Cómo capturar la respuesta

IO responde con `MSG_IO_FIN` cuando termina el sleep.

```c
t_mensaje* respuesta = recibir_mensaje(fd_io);
if (respuesta == NULL) {
    // IO cerró la conexión
}

if (respuesta->op_code == MSG_IO_FIN) {
    t_payload_io_fin* fin = (t_payload_io_fin*) respuesta->payload;
    uint32_t pid_terminado = fin->pid;
    // El proceso pid_terminado puede volver a READY
}

free_mensaje(respuesta);
```

### Logs que produce IO

```
[INFO]  IO: ## PID: 3 - Inicio de IO
[INFO]  IO: ## PID: 3 - Haciendo sleep por 2000 milisegundos
[INFO]  IO: ## PID: 3 - Fin de IO
```

---

## IO tipo STDOUT

### Flujo

```
KS  ──MSG_IO_STDOUT──►  IO  (imprime contenido)  ──MSG_IO_FIN──►  KS
```

### Cómo enviar el pedido desde el KS

El payload de `MSG_IO_STDOUT` no tiene struct — es `pid (4B)` seguido del string sin `\0`:

```c
#include <string.h>
#include <stdlib.h>
#include <utils/protocolo.h>
#include <utils/sockets.h>

// fd_io = file descriptor del socket conectado al proceso IO STDOUT

void enviar_stdout(int fd_io, uint32_t pid, char* contenido) {
    size_t   len          = strlen(contenido);
    uint32_t payload_size = sizeof(uint32_t) + len;

    void* payload = malloc(payload_size);
    memcpy(payload, &pid, sizeof(uint32_t));
    memcpy((char*)payload + sizeof(uint32_t), contenido, len);

    enviar_mensaje(fd_io, MSG_IO_STDOUT, payload, payload_size);
    free(payload);
}

// Uso:
enviar_stdout(fd_io, pid_del_proceso, "Hola desde el proceso 5");
```

### Cómo capturar la respuesta

Igual que SLEEP — IO responde con `MSG_IO_FIN`:

```c
t_mensaje* respuesta = recibir_mensaje(fd_io);
if (respuesta != NULL && respuesta->op_code == MSG_IO_FIN) {
    t_payload_io_fin* fin = (t_payload_io_fin*) respuesta->payload;
    uint32_t pid_terminado = fin->pid;
    // El proceso puede volver a READY
}
free_mensaje(respuesta);
```

### Logs que produce IO

```
[INFO]  IO: ## PID: 5 - Inicio de IO
[INFO]  IO: ## PID: 5 - Hola desde el proceso 5
[INFO]  IO: ## PID: 5 - Fin de IO
```

Y lo mismo se imprime por `stdout` del proceso IO.

---

## IO tipo STDIN

### Flujo

```
KS  ──MSG_IO_STDIN──►  IO  (lee del teclado)  ──MSG_IO_STDIN_DATOS──►  KS
```

IO **no** responde con `MSG_IO_FIN` — responde con `MSG_IO_STDIN_DATOS` que contiene los datos leídos.

### Cómo enviar el pedido desde el KS

Payload: struct `t_payload_io_stdin` definida en `protocolo.h`.

```c
t_payload_io_stdin pedido = {
    .pid     = pid_del_proceso,
    .n_bytes = 10,   // cantidad de bytes a leer del teclado
};

enviar_mensaje(fd_io, MSG_IO_STDIN, &pedido, sizeof(pedido));
```

### Cómo capturar la respuesta

IO responde con `MSG_IO_STDIN_DATOS`. El payload tiene el layout:
`{ uint32_t pid (4B) | uint32_t n_bytes (4B) | uint8_t datos[n_bytes] }`

```c
t_mensaje* respuesta = recibir_mensaje(fd_io);
if (respuesta != NULL && respuesta->op_code == MSG_IO_STDIN_DATOS) {
    uint32_t pid, n_bytes;
    memcpy(&pid,     respuesta->payload,                     sizeof(uint32_t));
    memcpy(&n_bytes, (char*)respuesta->payload + sizeof(uint32_t), sizeof(uint32_t));

    uint8_t* datos = (uint8_t*)respuesta->payload + sizeof(uint32_t) + sizeof(uint32_t);
    // 'datos' tiene exactamente n_bytes bytes:
    // - si el usuario escribió menos: los bytes restantes son '\0'
    // - si el usuario escribió más: se trunca a n_bytes

    // Acá el KS pasa 'datos' a Kernel Memory para escribir en la dirección lógica del proceso
}
free_mensaje(respuesta);
```

### Comportamiento con el largo de la entrada

| El usuario escribe | Resultado en `datos[]` |
|---|---|
| Exactamente `n_bytes` chars | Los `n_bytes` chars del usuario |
| Menos de `n_bytes` chars | Los chars del usuario + `\0` hasta completar `n_bytes` |
| Más de `n_bytes` chars | Solo los primeros `n_bytes` chars (truncado) |

### Logs que produce IO

```
[INFO]  IO: ## PID: 7 - Inicio de IO
[INFO]  IO: ## PID: 7 - Ingrese 10 caracteres:
[INFO]  IO: ## PID: 7 - Fin de IO
```

El prompt aparece en la consola del proceso IO. El usuario escribe ahí.

---

## Logs generados

Los logs se guardan en el directorio desde donde se corra el binario:

```
io_SLEEP.log
io_STDOUT.log
io_STDIN.log
```

**Logs obligatorios de la consigna** — deben aparecer exactamente con este formato:

```
## Conectado a Kernel Scheduler
## PID: <PID> - Inicio de IO
## PID: <PID> - Haciendo sleep por <TIEMPO> milisegundos    (solo SLEEP)
## PID: <PID> - <CONTENIDO A IMPRIMIR>                      (solo STDOUT)
## PID: <PID> - Ingrese <N> caracteres:                     (solo STDIN)
## PID: <PID> - Fin de IO
```

---

## Probar IO sin Kernel Scheduler real

Para verificar que IO conecta y envía la identificación sin tener el KS:

**Terminal 1 — servidor falso:**
```bash
nc -l 37214 | xxd
```

**Terminal 2 — correr IO:**
```bash
cd io && ./bin/io io.config SLEEP
```

En Terminal 2 debe aparecer `## Conectado a Kernel Scheduler`. En Terminal 1 llegan los bytes del mensaje de identificación en hex.

---

## Estado de implementación

| Funcionalidad | Estado |
|---|---|
| Conexión al KS e identificación | ✅ Check 1 |
| Loop de atención en `main.c` | ✅ Check 2 |
| IO tipo SLEEP | ✅ Check 2 — issue #23 |
| IO tipo STDOUT | ✅ Check 2 — issue #24 |
| IO tipo STDIN | ✅ Check 2 — issue #25 |
