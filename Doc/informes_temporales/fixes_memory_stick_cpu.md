# Fixes pendientes — Memory Stick: protocolo CPU↔MS

**Fecha:** 20/06/2026  
**Archivo a modificar:** `memory_stick/src/main.c`  
**Autor del apunte:** Nicolas Alessandro Barreiro

---

## Contexto

La CPU se conecta al Memory Stick con un socket persistente. El flujo completo para cada instrucción de memoria (`MOV_IN`, `MOV_OUT`, `COPY_MEM`) es:

```
CPU → MS : MSG_CPU_IDENTIFICACION  { cpu_id : uint32_t (network order) }
MS  → CPU: MSG_OK

── loop por cada instrucción de memoria ──
CPU → MS : MSG_LEER_MEMORIA    { dir_fisica:4, tamanio:4 }   (ambos en network order)
MS  → CPU: MSG_LEER_MEMORIA_RESP  { bytes[tamanio] }

CPU → MS : MSG_ESCRIBIR_MEMORIA  { dir_fisica:4, tamanio:4, datos[tamanio] }
MS  → CPU: MSG_OK
──────────────────────────────────────────
```

La conexión se cierra solo cuando la CPU se desconecta (fin del proceso o desconexión del KS).

Hay tres bugs que impiden que esto funcione. Los tres se corrigen en `atender_cpu` y en los handlers de lectura/escritura.

---

## Bug A — No se maneja `MSG_CPU_IDENTIFICACION`

La CPU envía este mensaje al conectarse. El switch actual no tiene el caso, cae en `default`, responde `MSG_ERROR` y cierra la conexión. La CPU setea `socket_memoria_usuario = -1` y todas las instrucciones de memoria fallan silenciosamente.

**Dónde está el problema:**

```c
// memory_stick/src/main.c — atender_cpu(), línea 227
switch (msg_cpu->op_code) {
    case MSG_MEMORY_WRITE:
        manejar_write(fd_cpu, msg_cpu);
        break;
    case MSG_MEMORY_READ:
        manejar_read(fd_cpu, msg_cpu);
        break;
    default:
        log_info(logger, "Opcode desconocido");
        enviar_mensaje(fd_cpu, MSG_ERROR, NULL, 0);  // ← cierra la CPU aquí
        break;
}
```

**Fix:** ver la sección "Solución completa" más abajo.

---

## Bug B — `atender_cpu` cierra la conexión después de UN mensaje

Incluso si se arregla el Bug A y la identificación funciona, el switch actual solo procesa un mensaje y luego cierra el socket. La CPU no puede hacer más de una operación de memoria.

**Dónde está el problema:**

```c
// memory_stick/src/main.c — atender_cpu()
t_mensaje* msg_cpu = recibir_mensaje(fd_cpu);   // lee UN mensaje
// ... switch ...
free_mensaje(msg_cpu);
close(fd_cpu);   // ← cierra inmediatamente
return NULL;
```

**Fix:** meter el switch en un `while(1)` que rompa cuando `recibir_mensaje` devuelva NULL (CPU desconectada).

---

## Bug C — Protocol mismatch entre CPU y MS

La CPU envía `MSG_LEER_MEMORIA` (36) y espera `MSG_LEER_MEMORIA_RESP` (37).  
La CPU envía `MSG_ESCRIBIR_MEMORIA` (38) y espera `MSG_OK`.

El MS solo tiene casos para `MSG_MEMORY_READ` (30) y `MSG_MEMORY_WRITE` (29), que son los que usa KM (no la CPU).

Nota: el payload tiene el mismo formato en ambos pares — solo difieren los op_codes.

| Sender | Op code enviado | Op code esperado de respuesta |
|---|---|---|
| KM | `MSG_MEMORY_READ` (30) | `MSG_MEMORY_READ_RESPUESTA` (31) |
| **CPU** | **`MSG_LEER_MEMORIA` (36)** | **`MSG_LEER_MEMORIA_RESP` (37)** |
| KM | `MSG_MEMORY_WRITE` (29) | `MSG_OK` |
| **CPU** | **`MSG_ESCRIBIR_MEMORIA` (38)** | **`MSG_OK`** |

---

## Solución completa

Los tres bugs se resuelven reescribiendo `atender_cpu` y agregando un handler de lectura para CPU que responde con el op_code correcto.

### Paso 1 — Agregar `manejar_read_cpu`

La función existente `manejar_read` responde con `MSG_MEMORY_READ_RESPUESTA`. Para la CPU necesitamos `MSG_LEER_MEMORIA_RESP`. Agregar esta función después de `manejar_read`:

```c
void manejar_read_cpu(int fd_cpu, t_mensaje* msg) {
    uint32_t direccion_n;
    uint32_t size_n;

    memcpy(&direccion_n, msg->payload, 4);
    memcpy(&size_n, (uint8_t*)msg->payload + 4, 4);

    uint32_t direccion = ntohl(direccion_n);
    uint32_t size      = ntohl(size_n);

    usleep(delay);

    if (direccion + size > memoria_global.tamanio) {
        enviar_mensaje(fd_cpu, MSG_ERROR, NULL, 0);
        log_info(logger, "La cantidad de bytes a leer es mayor al tamaño");
        return;
    }

    void* buffer = malloc(size);

    pthread_mutex_lock(&memoria_global.mutex);
    memcpy(buffer, (char*)memoria_global.buffer + direccion, size);
    pthread_mutex_unlock(&memoria_global.mutex);

    log_info(logger, "## Lectura de %u bytes", size);

    enviar_mensaje(fd_cpu, MSG_LEER_MEMORIA_RESP, buffer, size);  // ← op_code correcto para CPU

    free(buffer);
}
```

### Paso 2 — Reescribir `atender_cpu`

Reemplazar la función completa:

```c
void* atender_cpu(void* arg) {
    t_cpu_args* args = (t_cpu_args*) arg;
    int fd_cpu = args->fd_cpu;
    free(args);

    // Bug A — identificación de la CPU
    t_mensaje* id_msg = recibir_mensaje(fd_cpu);
    if (id_msg == NULL) {
        log_info(logger, "CPU desconectada antes de identificarse");
        close(fd_cpu);
        return NULL;
    }
    if (id_msg->op_code != MSG_CPU_IDENTIFICACION) {
        log_info(logger, "Primer mensaje no es identificacion: %u", id_msg->op_code);
        enviar_mensaje(fd_cpu, MSG_ERROR, NULL, 0);
        free_mensaje(id_msg);
        close(fd_cpu);
        return NULL;
    }

    uint32_t cpu_id_n;
    memcpy(&cpu_id_n, id_msg->payload, 4);
    uint32_t cpu_id = ntohl(cpu_id_n);
    free_mensaje(id_msg);

    log_info(logger, "## CPU %u Conectada", cpu_id);
    enviar_mensaje(fd_cpu, MSG_OK, NULL, 0);

    // Bug B + Bug C — loop por múltiples mensajes con op_codes correctos
    while (1) {
        t_mensaje* msg = recibir_mensaje(fd_cpu);
        if (msg == NULL) {
            log_info(logger, "CPU %u desconectada", cpu_id);
            break;
        }

        switch (msg->op_code) {
            case MSG_LEER_MEMORIA:      // CPU usa este op_code (36)
                manejar_read_cpu(fd_cpu, msg);
                break;
            case MSG_ESCRIBIR_MEMORIA:  // CPU usa este op_code (38)
                manejar_write(fd_cpu, msg);  // responde MSG_OK — está bien
                break;
            case MSG_MEMORY_READ:       // KM usa este op_code (30) — mantener compatibilidad
                manejar_read(fd_cpu, msg);
                break;
            case MSG_MEMORY_WRITE:      // KM usa este op_code (29) — mantener compatibilidad
                manejar_write(fd_cpu, msg);
                break;
            default:
                log_info(logger, "CPU %u: opcode desconocido %u", cpu_id, msg->op_code);
                enviar_mensaje(fd_cpu, MSG_ERROR, NULL, 0);
                break;
        }

        free_mensaje(msg);
    }

    close(fd_cpu);
    return NULL;
}
```

### Paso 3 — Agregar la forward declaration

Al inicio del archivo, donde están las declaraciones de funciones (línea ~15), agregar:

```c
void manejar_read_cpu(int fd_cpu, t_mensaje* msg);
```

---

## Verificación rápida

Una vez aplicado el fix, el flujo debería verse así en los logs del MS:

```
[MS] ## Conectado a Kernel Memory
[MS] Escuchando CPUs en puerto 8003
[MS] ## CPU 1 Conectada
[MS] ## Lectura de 4 bytes
[MS] ## Escritura de 4 bytes
[MS] CPU 1 desconectada
```

Si sigue sin funcionar, verificar que la CPU esté configurada con el IP y puerto del MS correcto en su archivo de config (`MEMORY_STICK_IP`, `MEMORY_STICK_PORT`).

---

## Por qué la rama `integracion-cpu-check3` no sirve

La rama fue revisada el 20/06/2026. Los cambios en `memory_stick/src/main.c` de esa rama:
- Agregan lectura de IP desde config y un struct `t_ms_identificacion`
- **No** agregan `MSG_CPU_IDENTIFICACION`
- **No** agregan el loop en `atender_cpu`
- **No** agregan `MSG_LEER_MEMORIA` / `MSG_ESCRIBIR_MEMORIA`
- Introducen un bug nuevo: `htonl(ms.ip)` donde `ms.ip` es `char[32]` — inválido

Además la rama modifica masivamente el Kernel Scheduler y eliminaría todo el trabajo del CK3 (CMN, herencia, compactación, STDOUT/STDIN). **No mergear.**
