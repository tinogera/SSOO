# Utils — Guía de uso para el equipo

**Módulo:** `utils/`
**Responsable:** Nicolas
**Estado:** Check 1 — sockets y protocolo base implementados

---

## ¿Qué es utils?

`utils` es la librería compartida del proyecto. Todos los módulos (CPU, IO, Kernel Scheduler, Kernel Memory, Memory Stick, Swap) la incluyen como dependencia estática.

Genera `utils/lib/libutils.a`, que se enlaza automáticamente cuando hacen `make` en su módulo.

**No tienen que tocar el Makefile de su módulo** — ya está configurado para buscar utils en `../utils/lib` y los headers en `../utils/src`.

---

## Cómo incluir utils en su módulo

En cualquier `.c` de su módulo:

```c
#include <utils/sockets.h>    // sockets, t_mensaje, serialización
#include <utils/protocolo.h>  // enum op_code con los tipos de mensaje
```

---

## Compilación

Siempre compilar utils **antes** de compilar su módulo, porque los demás dependen de la librería estática que genera:

```bash
# Desde la raíz del proyecto
cd utils && make && cd ..
cd su_modulo && make
```

Si cambian algo en utils, tienen que recompilar utils antes de recompilar su módulo.

---

## Protocolo de mensajes

Todos los mensajes entre módulos siguen este formato en el socket:

```
[ op_code (4 bytes) ][ payload_size (4 bytes) ][ payload (variable) ]
```

- `op_code`: identifica qué tipo de mensaje es (ver `protocolo.h`)
- `payload_size`: cantidad de bytes del payload (puede ser 0)
- `payload`: los datos del mensaje

### Códigos de operación disponibles (`protocolo.h`)

| Código | Valor | Descripción | Usado por |
|---|---|---|---|
| `MSG_IO_IDENTIFICACION` | 0 | IO se presenta al Kernel Scheduler con su tipo | IO → Kernel Scheduler |

> **Check 2 y Check 3:** a medida que cada módulo necesite nuevos tipos de mensaje, se agregan en `protocolo.h`. Coordinar con Nicolas para no pisar valores del enum.

---

## Funciones disponibles

### Crear un servidor (escuchar conexiones entrantes)

```c
int fd_servidor = crear_servidor(37214);
if (fd_servidor < 0) {
    // error — ver perror en stderr
}
```

Retorna el file descriptor del servidor. Pasa ese fd a `aceptar_conexion` para esperar clientes.

### Aceptar una conexión

```c
int fd_cliente = aceptar_conexion(fd_servidor);
```

Bloquea hasta que alguien se conecte. Retorna el fd del cliente. Llamar una vez por cliente esperado, o en un loop para múltiples clientes.

### Conectarse a otro módulo

```c
int fd = conectar_a_servidor("127.0.0.1", 37214);
if (fd < 0) {
    // error — el servidor no está arriba todavía
}
```

Usar la IP y puerto del archivo de configuración del módulo.

### Enviar un mensaje

```c
// Sin payload (solo op_code)
enviar_mensaje(fd, MSG_IO_IDENTIFICACION, NULL, 0);

// Con payload (string serializado)
uint32_t size;
void* payload = serializar_string("SLEEP", &size);
enviar_mensaje(fd, MSG_IO_IDENTIFICACION, payload, size);
free(payload);
```

### Recibir un mensaje

```c
t_mensaje* msg = recibir_mensaje(fd);
if (msg == NULL) {
    // conexión cerrada por el otro lado
}

switch (msg->op_code) {
    case MSG_IO_IDENTIFICACION:
        char* tipo = deserializar_string(msg->payload);
        // ... usar tipo ...
        free(tipo);
        break;
    // otros casos...
}

free_mensaje(msg);  // siempre liberar al terminar
```

### Serialización de strings

```c
// Serializar (para poner en el payload de un mensaje)
uint32_t size;
void* buf = serializar_string("hola", &size);
enviar_mensaje(fd, algún_opcode, buf, size);
free(buf);

// Deserializar (al recibir un mensaje con string en el payload)
char* str = deserializar_string(msg->payload);
// ... usar str ...
free(str);
```

---

## Ejemplo completo: módulo servidor

```c
#include <utils/sockets.h>
#include <utils/protocolo.h>

int fd_servidor = crear_servidor(37214);

// Aceptar una conexión
int fd_cliente = aceptar_conexion(fd_servidor);

// Recibir identificación de IO
t_mensaje* msg = recibir_mensaje(fd_cliente);
if (msg->op_code == MSG_IO_IDENTIFICACION) {
    char* tipo = deserializar_string(msg->payload);
    // tipo es "STDIN", "STDOUT" o "SLEEP"
    free(tipo);
}
free_mensaje(msg);
```

## Ejemplo completo: módulo cliente

```c
#include <utils/sockets.h>
#include <utils/protocolo.h>

int fd = conectar_a_servidor("127.0.0.1", 37214);

// Enviar identificación
uint32_t size;
void* payload = serializar_string("SLEEP", &size);
enviar_mensaje(fd, MSG_IO_IDENTIFICACION, payload, size);
free(payload);
```

---

## Manejo de desconexión

`recibir_mensaje` retorna `NULL` cuando el otro extremo cerró la conexión. Siempre checkear:

```c
t_mensaje* msg = recibir_mensaje(fd);
if (msg == NULL) {
    log_error(logger, "Conexión cerrada inesperadamente");
    // manejar la desconexión según el módulo
}
```

---

## Actualizaciones previstas

| Check | Qué se va a agregar |
|---|---|
| Check 2 | Nuevos `op_code` para planificación, IO real, y fetch de instrucciones |
| Check 3 | Serialización de estructuras complejas (contexto CPU, segmentos) |

Cada vez que se agregue funcionalidad, este documento se actualiza.
