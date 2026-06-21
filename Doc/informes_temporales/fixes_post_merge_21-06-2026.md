# Fixes post-merge — 21/06/2026

Revisión del estado de `develop` tras el merge de `integracion-cpu-check3` por Juan Manuel. Se documentan los bugs encontrados durante la revisión y los cambios realizados para resolverlos.

---

## Memory Stick — protocolo CPU↔MS (Juan Manuel)

Commits: `edb988c`, `746d5e9`, `f26c9ce`

El Memory Stick no tenía implementado el protocolo de identificación ni el loop de conexión persistente que la CPU espera al conectarse. Tampoco manejaba los op_codes que la CPU usa para leer y escribir memoria.

### Problema 1 — Identificación de CPU no manejada

Cuando la CPU se conecta al Memory Stick, el primer mensaje que envía es `MSG_CPU_IDENTIFICACION` con su `cpu_id`. El switch de `atender_cpu` no tenía ese case, caía en `default`, respondía `MSG_ERROR` y cerraba la conexión. Desde ese momento la CPU guardaba `socket_memoria_usuario = -1` y todas las instrucciones de memoria (`MOV_IN`, `MOV_OUT`, `COPY_MEM`) fallaban silenciosamente.

**Fix:** se agregó el case `MSG_CPU_IDENTIFICACION` antes del loop. Extrae el `cpu_id` del payload (4 bytes, network order), loguea `## CPU %u Conectada` y responde `MSG_OK`.

### Problema 2 — Conexión cerrada tras un solo mensaje

`atender_cpu` leía un mensaje, lo procesaba y cerraba el socket con `close(fd_cpu)`. La conexión entre CPU y Memory Stick es persistente durante toda la ejecución del proceso: la CPU envía múltiples lecturas y escrituras sobre el mismo fd.

**Fix:** el procesamiento de mensajes se movió a un `while(1)` que rompe cuando `recibir_mensaje` devuelve NULL (CPU desconectada). El `close(fd_cpu)` pasa al final del loop.

### Problema 3 — Op codes incorrectos en el switch

La CPU envía `MSG_LEER_MEMORIA` (36) y espera `MSG_LEER_MEMORIA_RESP` (37). La CPU envía `MSG_ESCRIBIR_MEMORIA` (38) y espera `MSG_OK`. El switch del Memory Stick solo tenía `MSG_MEMORY_READ` (30) y `MSG_MEMORY_WRITE` (29), que son los op codes que usa el Kernel Memory, no la CPU.

El formato del payload es idéntico en ambos pares, solo difieren los números de op code.

**Fix:** se agregaron `case MSG_LEER_MEMORIA:` y `case MSG_ESCRIBIR_MEMORIA:` al loop. Para lectura se agregó la función `manejar_read_cpu` que tiene la misma lógica que `manejar_read` pero responde con `MSG_LEER_MEMORIA_RESP` en lugar de `MSG_MEMORY_READ_RESPUESTA`. Para escritura, `manejar_write` ya respondía `MSG_OK`, así que se reutilizó directamente.

Los cases `MSG_MEMORY_READ` y `MSG_MEMORY_WRITE` se mantienen para preservar la compatibilidad con el Kernel Memory, que sigue usando esos op codes por sus conexiones persistentes.

---

## Cleanup del merge — duplicados y código muerto

Commit: `0b5e6fb`

El merge de `integracion-cpu-check3` introdujo varias definiciones duplicadas como artefacto de la resolución de conflictos.

### `utils/src/utils/protocolo.h`

`t_payload_syscall_mem_alloc` y `t_payload_syscall_mem_free` quedaron definidos dos veces: una como typedefs junto a las structs de las que derivan (líneas 177/183) y otra vez unos renglones más abajo (185/186). Se eliminó la segunda aparición.

### `cpu/src/cpu_syscalls.c`

`#include <arpa/inet.h>` aparecía dos veces al inicio del archivo. Se eliminó la segunda línea.

### `cpu/src/cpu_devolucion.c`

Mismo problema: `#include <arpa/inet.h>` duplicado. Se eliminó la segunda línea.

---

## Código muerto en `enviar_syscall_io_memoria`

Commit: `65a893e` — archivo: `cpu/src/cpu_syscalls.c`

La función `enviar_syscall_io_memoria` (que construye el payload para `STDOUT` y `STDIN`) tenía dos sentencias `return true;` consecutivas. La segunda es inalcanzable. Se eliminó.

```c
// antes
enviar_mensaje(socket_kernel, op_code, &payload, sizeof(payload));
return true;
return true;   // ← eliminado

// después
enviar_mensaje(socket_kernel, op_code, &payload, sizeof(payload));
return true;
```

---

## Comentario en `contexto_actual`

Commit: `65a893e` — archivo: `cpu/src/cpu_contexto.h`

El header declaraba `extern t_contexto* contexto_actual` sin que exista ninguna definición de esa variable en ningún `.c` ni ningún uso en el código. Se reemplazó el comentario original por uno que deja claro el estado:

```c
// Declarado pero sin definición ni uso actual. Posible implementación futura
// para acceder al contexto del proceso en ejecución desde otros módulos de CPU.
extern t_contexto* contexto_actual;
```

---

## Bug en la MMU — SEGMENT_MAX_SIZE ignorado

Commit: `7440ba6` — archivos: `cpu/src/cpu_mmu.c`, `cpu/src/cpu_mmu.h`, `cpu/src/main.c`

### El problema

`traducir_direccion_logica` necesita saber el tamaño de cada segmento para calcular a qué segmento apunta una dirección lógica:

```
id_segmento  = dir_logica / SEGMENT_MAX_SIZE
desplazamiento = dir_logica % SEGMENT_MAX_SIZE
```

Tras el merge, `traducir_direccion_logica` obtenía ese valor llamando a `get_tamanio_max_segmento(contexto)`, que sumaba los tamaños de **todos** los segmentos del proceso:

```c
uint32_t get_tamanio_max_segmento(t_contexto* contexto) {
    uint32_t tamanio_max_segmento = 0;
    for (...) {
        tamanio_max_segmento += limite[i] - base[i];
    }
    return tamanio_max_segmento;
}
```

Esto funciona si el proceso tiene exactamente un segmento, pero falla en cuanto hay más. Ejemplo con `SEGMENT_MAX_SIZE = 256` y dos segmentos:

- `get_tamanio_max_segmento()` devuelve 512
- `dir_logica = 300` → `id = 300 / 512 = 0` ❌ debería ser `300 / 256 = 1`
- La MMU busca el segmento 0 y accede a memoria incorrecta

El config ya leía `SEGMENT_MAX_SIZE` en `cpu/src/main.c` pero nunca lo usaba (de ahí el warning `-Wunused-but-set-variable`).

### El fix

Se agregó un global estático en `cpu_mmu.c` con valor por defecto 256 y una función para setearlo:

```c
// cpu_mmu.c
static uint32_t g_segment_max_size = 256;

void set_segment_max_size(uint32_t tamanio) {
    g_segment_max_size = tamanio;
}
```

Se declaró en `cpu_mmu.h`:

```c
void set_segment_max_size(uint32_t tamanio);
```

En `cpu/src/main.c`, justo después de leer el config:

```c
set_segment_max_size(tamanio_max_segmento);
```

En `traducir_direccion_logica`, se reemplazó la llamada a `get_tamanio_max_segmento(contexto)` por `g_segment_max_size`. La función `get_tamanio_max_segmento` se eliminó por completo.

El módulo CPU compila sin warnings ni errores después del fix.

---

## Estado final de `develop`

Todos los módulos compilan limpio. Los requisitos del CK3 están completos en `develop` y listo para merge a `main`.

| Módulo | Estado |
|---|---|
| utils | ✅ |
| kernel_memory | ✅ |
| kernel_scheduler | ✅ |
| cpu | ✅ |
| memory_stick | ✅ |
| io | ✅ |
| swap | ✅ |
