# Feat: soporte multi-Memory Stick en CPU — 06/07/2026

**Módulo:** CPU  
**Archivos modificados:**
- `cpu/src/main.c`
- `cpu/src/cpu_ciclo.h` / `cpu/src/cpu_ciclo.c`
- `cpu/src/cpu_memoria.h` / `cpu/src/cpu_memoria.c`

**Commit:** `8565a44`

## Problema

La CPU conectaba a un único Memory Stick, leído de las claves `IP_MEMORY_STICK` y `PUERTO_MEMORY_STICK` del config. Si el sistema tiene múltiples MS (cada uno con su propio puerto), los datos que KM asigna al segundo o tercer MS son inaccesibles desde la CPU: `cpu_memoria.c` enviaba siempre al mismo fd independientemente del MS donde estuviera el segmento.

El campo `id_memory_stick` en `t_traduccion_mmu` ya existía y era correctamente llenado por `traducir_direccion_logica`, pero nunca se usaba para seleccionar el socket — se ignoraba.

## Diseño del fix

### Config — claves indexadas con fallback

Se adopta el formato `IP_MEMORY_STICK_<N>` / `PUERTO_MEMORY_STICK_<N>` para especificar N Memory Sticks en orden. El índice N **debe coincidir** con el `id_memory_stick` que KM asigna a cada MS (KM los numera en orden de conexión, empezando en 0).

Para mantener compatibilidad con configs existentes de un solo MS (que usan `IP_MEMORY_STICK` sin índice), si `IP_MEMORY_STICK_0` no existe se intenta con `IP_MEMORY_STICK`.

Ejemplo de config con dos MS:
```
IP_MEMORY_STICK_0=127.0.0.1
PUERTO_MEMORY_STICK_0=8003
IP_MEMORY_STICK_1=127.0.0.1
PUERTO_MEMORY_STICK_1=8004
```

### main.c — array de fds

Se reemplaza la variable `socket_memoria_usuario` (un único `int`) por un array `int sockets_ms[CPU_MAX_MS]` donde `CPU_MAX_MS = 8`. El índice es el `id_memory_stick`. Si un MS no está configurado o falla la conexión, su entrada queda en -1.

```c
for (int idx = 0; idx < CPU_MAX_MS; idx++) {
    // intentar con IP_MEMORY_STICK_<idx> (o sin índice para idx=0)
    // conectar, hacer handshake MSG_CPU_IDENTIFICACION
    // guardar fd en sockets_ms[idx]
}
```

### cpu_memoria.c — selección de fd por id_memory_stick

Se agrega el helper:
```c
static int fd_para_ms(int* sockets_ms, int n_sockets_ms, uint32_t id_ms) {
    if ((int)id_ms < n_sockets_ms) return sockets_ms[id_ms];
    return -1;
}
```

Las tres funciones de instrucción (`ejecutar_mov_in`, `ejecutar_mov_out`, `ejecutar_copy_mem`) ahora reciben `int* sockets_ms, int n_sockets_ms` en lugar de `int socket_memoria`, y tras la traducción MMU llaman a `fd_para_ms` con `traduccion.id_memory_stick`.

Para `COPY_MEM` en particular, origen y destino pueden estar en MS distintos — se selecciona el fd correcto para cada uno por separado:
```c
int fd_origen  = fd_para_ms(sockets_ms, n_sockets_ms, origen.id_memory_stick);
int fd_destino = fd_para_ms(sockets_ms, n_sockets_ms, destino.id_memory_stick);
memoria_read(fd_origen,  ...);
memoria_write(fd_destino, ...);
```

### Cambios de firma propagados

`ejecutar_instruccion_memoria` en `cpu_memoria.h` y `ejecutar_ciclo_proceso` en `cpu_ciclo.h` actualizaron sus firmas. No hay impacto en otros módulos: el único caller de `ejecutar_ciclo_proceso` es `main.c`.

## Verificación

El módulo compila sin warnings. El fallback a claves sin índice mantiene compatibilidad con el config existente de un solo MS.
