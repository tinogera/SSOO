# Utils — Guía de uso para el equipo

**Módulo:** `utils/`  
**Responsable:** Nicolas  
**Estado:** Check 2 — op_codes de IO y Mutex agregados

---

## ¿Qué es utils?

`utils` es la librería compartida del proyecto. Todos los módulos (CPU, IO, Kernel Scheduler, Kernel Memory, Memory Stick, Swap) la incluyen como dependencia estática.

Genera `utils/lib/libutils.a`, que se enlaza automáticamente cuando hacen `make` en su módulo.

---

## Compilación

Siempre compilar utils **antes** de compilar su módulo:

```bash
cd utils && make && cd ..
cd su_modulo && make
```

Si cambian algo en utils, recompilen utils antes que su módulo.

---

## Cómo incluir utils

```c
#include <utils/sockets.h>    // sockets, t_mensaje, serialización
#include <utils/protocolo.h>  // enum op_code con todos los tipos de mensaje
```

---

## Esquema de conexiones del sistema

```
                         ┌─────────────────────────────────────┐
                         │         Kernel Memory (KM)          │
                         │  escucha en KERNEL_MEMORY_PORT      │
                         └──┬──────────┬────────────┬──────────┘
             se conecta a KM│          │            │           │
         ┌──────────────────┘    ┌─────┘      ┌────┘     ┌─────┘
         ▼                       ▼            ▼           ▼
  ┌─────────────┐        ┌────────────┐  ┌────────┐  ┌────────┐
  │Kernel Sched.│        │    CPU     │  │  Swap  │  │Memory  │
  │(KS)         │        │            │  │        │  │ Stick  │
  └──────┬──────┘        └────────────┘  └────────┘  └────────┘
         │ escucha en KS_PORT
         │ ← se conectan CPU e IO
    ┌────┴────┐
    │         │
    ▼         ▼
┌───────┐ ┌───────┐
│  CPU  │ │  IO   │
└───────┘ └───────┘
```

---

## Protocolo de mensajes

Todos los mensajes entre módulos usan el mismo formato:

```
[ op_code (4 bytes) ][ payload_size (4 bytes) ][ payload (variable) ]
```

- `op_code`: identifica el tipo de mensaje (ver tabla abajo)
- `payload_size`: cantidad de bytes del payload (0 si no hay datos)
- `payload`: los datos del mensaje — formato específico por op_code

**Formato de campos numéricos en el payload:** `uint32_t` en network byte order (`htonl`/`ntohl`).  
**Formato de strings en el payload:** null-terminated, siempre al final del payload.

---

## Tabla de op_codes

### Check 1 — Identificación inicial

| Código | Valor | Dirección | Payload |
|--------|-------|-----------|---------|
| `MSG_IO_IDENTIFICACION` | 0 | IO → KS | `char tipo[]` — `"SLEEP"`, `"STDOUT"` o `"STDIN"` |
| `MSG_CPU_IDENTIFICACION` | 1 | CPU → KS | `char id_cpu[]` — identificador de la CPU |
| `MSG_KS_IDENTIFICACION` | 2 | KS → KM | sin payload |
| `MSG_OK` | 3 | cualquiera | sin payload — respuesta exitosa |
| `MSG_ERROR` | 4 | cualquiera | sin payload — respuesta de error |
| `MSG_MEMORY_STICK_IDENTIFICACION` | 5 | MS → KM | sin payload |
| `MSG_SWAP_IDENTIFICACION` | 6 | Swap → KM | sin payload |
| `MSG_CPU_A_KERNEL_MEMORY` | 7 | CPU → KM | sin payload |

### Check 2 — IO

| Código | Valor | Dirección | Payload |
|--------|-------|-----------|---------|
| `MSG_IO_SLEEP` | 8 | KS → IO SLEEP | `uint32_t pid` + `uint32_t tiempo_ms` |
| `MSG_IO_STDOUT` | 9 | KS → IO STDOUT | `uint32_t pid` + `char contenido[]` |
| `MSG_IO_STDIN` | 10 | KS → IO STDIN | `uint32_t pid` + `uint32_t n_bytes` |
| `MSG_IO_FIN` | 11 | IO → KS | `uint32_t pid` |
| `MSG_IO_STDIN_DATOS` | 12 | IO → KS | `uint32_t pid` + `uint32_t n_bytes` + `uint8_t datos[]` |

### Check 2 — Mutex

| Código | Valor | Dirección | Payload |
|--------|-------|-----------|---------|
| `MSG_MUTEX_CREATE` | 13 | CPU → KS | `uint32_t pid` + `char nombre[]` |
| `MSG_MUTEX_LOCK` | 14 | CPU → KS | `uint32_t pid` + `char nombre[]` |
| `MSG_MUTEX_UNLOCK` | 15 | CPU → KS | `uint32_t pid` + `char nombre[]` |

---

## Flujos de comunicación — Check 2

### IO tipo SLEEP

```
KS                              IO (SLEEP)
│                                    │
│──── MSG_IO_SLEEP ─────────────────►│  payload: { pid, tiempo_ms }
│                               usleep(tiempo_ms * 1000)
│◄─── MSG_IO_FIN ────────────────────│  payload: { pid }
│                                    │
```

**Log obligatorio en IO:**
```
## PID: <PID> - Inicio de IO
## PID: <PID> - Haciendo sleep por <TIEMPO> milisegundos
## PID: <PID> - Fin de IO
```

---

### IO tipo STDOUT

```
KS                              IO (STDOUT)
│                                    │
│──── MSG_IO_STDOUT ────────────────►│  payload: { pid, contenido\0 }
│                               printf / log contenido
│◄─── MSG_IO_FIN ────────────────────│  payload: { pid }
│                                    │
```

**Log obligatorio en IO:**
```
## PID: <PID> - Inicio de IO
## PID: <PID> - <CONTENIDO A IMPRIMIR>
## PID: <PID> - Fin de IO
```

---

### IO tipo STDIN

```
KS                              IO (STDIN)
│                                    │
│──── MSG_IO_STDIN ─────────────────►│  payload: { pid, n_bytes }
│                               leer teclado, truncar/padear a n_bytes
│◄─── MSG_IO_STDIN_DATOS ────────────│  payload: { pid, n_bytes, datos[] }
│                                    │
```

**Log obligatorio en IO:**
```
## PID: <PID> - Inicio de IO
## PID: <PID> - Ingrese <N> caracteres:
## PID: <PID> - Fin de IO
```

---

### Mutex (MUTEX_CREATE / LOCK / UNLOCK)

```
CPU                             KS
│                                │
│──── MSG_MUTEX_CREATE ─────────►│  payload: { pid, nombre\0 }
│◄─── MSG_OK ────────────────────│
│                                │
│──── MSG_MUTEX_LOCK ───────────►│  payload: { pid, nombre\0 }
│   (si mutex libre)             │
│◄─── MSG_OK ────────────────────│  → CPU sigue ejecutando
│   (si mutex tomado)            │
│                    proceso pasa a BLOCK, CPU no recibe respuesta
│                    hasta que otro proceso haga UNLOCK
│                                │
│──── MSG_MUTEX_UNLOCK ─────────►│  payload: { pid, nombre\0 }
│◄─── MSG_OK ────────────────────│
│                                │
```

**Logs obligatorios en KS:**
```
## (<PID>) Toma el Mutex <NOMBRE>
## (<PID>) Libera el Mutex <NOMBRE>
```

---

## Formato de payload — detalle por tipo

### Solo campos numéricos (SLEEP, STDIN, FIN)

Usar struct empaquetado en `protocolo.h`:

```c
typedef struct __attribute__((packed)) { uint32_t pid; uint32_t tiempo_ms; } t_payload_io_sleep;
typedef struct __attribute__((packed)) { uint32_t pid; uint32_t n_bytes;   } t_payload_io_stdin;
typedef struct __attribute__((packed)) { uint32_t pid;                      } t_payload_io_fin;
```

**Enviar:**
```c
t_payload_io_sleep p = { .pid = pid, .tiempo_ms = tiempo_ms };
enviar_mensaje(fd, MSG_IO_SLEEP, &p, sizeof(p));
```

**Recibir:**
```c
t_payload_io_sleep* p = (t_payload_io_sleep*) msg->payload;
uint32_t pid       = p->pid;
uint32_t tiempo_ms = p->tiempo_ms;
```

### Campos numérico + string (STDOUT, MUTEX_*)

Layout en memoria: `[ uint32_t pid (4 bytes) ][ string\0 (variable) ]`

**Enviar:**
```c
// helper disponible en sockets.h (a agregar en CK2)
uint32_t size;
void* payload = serializar_uint32_string(pid, nombre, &size);
enviar_mensaje(fd, MSG_MUTEX_LOCK, payload, size);
free(payload);
```

**Recibir:**
```c
uint32_t pid;
char* nombre;
deserializar_uint32_string(msg->payload, &pid, &nombre);
// nombre apunta dentro del payload — usar o strdup() si necesitás copia propia
```

### STDIN_DATOS (numérico + buffer raw)

Layout: `[ uint32_t pid (4B) ][ uint32_t n_bytes (4B) ][ datos (n_bytes B) ]`

```c
// Armar manualmente
uint32_t psize = 4 + 4 + n_bytes;
void* payload  = malloc(psize);
memcpy(payload,     &pid,     4);
memcpy(payload + 4, &n_bytes, 4);
memcpy(payload + 8, datos,    n_bytes);
enviar_mensaje(fd, MSG_IO_STDIN_DATOS, payload, psize);
free(payload);
```

---

## Funciones disponibles en sockets.h

### Servidor / cliente

```c
int crear_servidor(int puerto);               // retorna fd servidor, -1 si falla
int aceptar_conexion(int fd_servidor);        // bloquea hasta que llegue un cliente
int conectar_a_servidor(char* ip, int puerto);// retorna fd conexión, -1 si falla
```

### Enviar y recibir

```c
void       enviar_mensaje(int fd, uint32_t op_code, void* payload, uint32_t size);
t_mensaje* recibir_mensaje(int fd);   // retorna NULL si la conexión se cerró
void       free_mensaje(t_mensaje* msg);
```

### Serialización

```c
void*  serializar_string(char* str, uint32_t* out_size);  // incluye el '\0'
char*  deserializar_string(void* payload);                 // retorna copia en heap
```

### Manejo de desconexión

`recibir_mensaje` retorna `NULL` cuando el otro extremo cerró el socket. Siempre verificar:

```c
t_mensaje* msg = recibir_mensaje(fd);
if (msg == NULL) {
    // conexión cerrada — manejar según el módulo
    break;
}
```

---

## Actualizaciones previstas — Check 3

| Op_code | Descripción |
|---------|-------------|
| `MSG_DESPACHAR_PROCESO` | KS → CPU: mandar a ejecutar un PID |
| `MSG_DEVOLVER_PROCESO` | CPU → KS: fin de ciclo + motivo |
| `MSG_FETCH_INSTRUCCION` | CPU → KM: pedir instrucción en PC actual |
| `MSG_RESPUESTA_INSTRUCCION` | KM → CPU: string de la instrucción |
| `MSG_CREAR_PROCESO` | KS → KM: inicializar contexto para un PID |
| `MSG_MEM_ALLOC` / `MSG_MEM_FREE` | CPU → KS (syscalls de memoria) |
| `MSG_LEER_MEMORIA` / `MSG_ESCRIBIR_MEMORIA` | CPU → Memory Stick |
| `MSG_COMPACTAR` / `MSG_FIN_COMPACTACION` | KM ↔ KS |
| `MSG_SUSPENDER_PROCESO` | KM → Swap |
| `MSG_BSOD` | KM → KS |
