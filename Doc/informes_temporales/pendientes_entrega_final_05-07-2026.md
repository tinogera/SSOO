# Pendientes para la entrega final — 05/07/2026

**Fecha límite:** 11/07/2026 (6 días)  
**Elaborado por:** Nicolas Alessandro Barreiro  
**Base:** revisión directa del código en `develop` — no de la documentación

---

## Contexto

El CK3 se cerró con todos los módulos compilando y el flujo básico (MUTEX → SLEEP → EXIT) funcionando. Sin embargo, la prueba de integración usada en CK3 **no tocó memoria física en absoluto** (sin MEM_ALLOC, MOV_IN/OUT, STDOUT/STDIN real). Al revisar el código para la entrega final aparecieron dos bugs críticos que harían fallar cualquier script que use instrucciones de memoria.

---

## Bug 1 — Memory Stick no escucha a Kernel Memory (CRÍTICO)

**Responsable:** Juan Manuel Fernandez  
**Archivo:** `memory_stick/src/main.c`

### El problema

Cuando el MS arranca, conecta a KM, envía `MSG_MEMORY_STICK_IDENTIFICACION` y recibe `MSG_OK`. Después de eso, `fd_km` **nunca se vuelve a leer**. El MS solo crea un servidor y atiende conexiones de CPUs.

Mientras tanto, KM usa `ms->fd` (el fd de la conexión aceptada cuando MS se conectó) para enviar `MSG_MEMORY_READ` y `MSG_MEMORY_WRITE` cada vez que necesita leer o escribir datos físicos. Como el MS no está leyendo de su `fd_km`, **KM se bloquea para siempre** en el `recibir_mensaje` que sigue al envío.

### Qué falla en la práctica

- Cualquier script con `MEM_ALLOC` → KM intenta asignar el segmento, y cuando necesita verificar espacio o inicializar, puede necesitar acceso físico → deadlock.
- `STDOUT` real: KS pide a KM los bytes con `MSG_LEER_DATOS`, KM llama `leer_fisico()` → `ms_leer()` → envía a `ms->fd` y espera respuesta que nunca llega.
- `STDIN` real: igual, vía `escribir_fisico()` → `ms_escribir()`.
- `Compactación`: `compactar()` hace `leer_fisico()` y `escribir_fisico()` para mover segmentos.
- `Suspensión/des-suspensión`: `suspender_proceso()` llama `leer_fisico()` para copiar datos a Swap.

### Fix necesario

Agregar un hilo en `main()` de Memory Stick que lea de `fd_km` en un loop y responda los pedidos de KM. Lanzarlo justo después del handshake, antes del loop de aceptar CPUs:

```c
// En memory_stick/src/main.c

typedef struct {
    int fd_km;
} t_km_args;

void* atender_km(void* arg) {
    t_km_args* a = (t_km_args*) arg;
    int fd = a->fd_km;
    free(a);

    while (1) {
        t_mensaje* msg = recibir_mensaje(fd);
        if (!msg) {
            log_warning(logger, "KM cerró la conexión");
            break;
        }
        switch (msg->op_code) {
            case MSG_MEMORY_READ:
                manejar_read(fd, msg);   // ya existe — responde MSG_MEMORY_READ_RESPUESTA
                break;
            case MSG_MEMORY_WRITE:
                manejar_write(fd, msg);  // ya existe — responde MSG_OK
                break;
            default:
                log_warning(logger, "KM: opcode inesperado %u", msg->op_code);
                enviar_mensaje(fd, MSG_ERROR, NULL, 0);
                break;
        }
        free_mensaje(msg);
    }
    return NULL;
}
```

Y en `main()`, después de recibir `MSG_OK` del handshake con KM y **antes** del loop de aceptar CPUs:

```c
t_km_args* km_args = malloc(sizeof(t_km_args));
km_args->fd_km = fd_km;
pthread_t t_km;
pthread_create(&t_km, NULL, atender_km, km_args);
pthread_detach(t_km);
```

**Nota:** `manejar_read` y `manejar_write` ya existen y tienen la lógica correcta (leen/escriben de `memoria_global.buffer`, aplican delay, usan mutex). No hay que cambiarlos.

---

## Bug 2 — MEM_ALLOC / MEM_FREE: flujo roto entre CPU y KS (CRÍTICO)

**Responsables:** Kevin Castillo (CPU) + Nicolas Alessandro Barreiro (KS)  
**Archivos:**
- `cpu/src/cpu_ciclo.c`
- `cpu/src/cpu_syscalls.c`
- `kernel_scheduler/src/main.c`

### El problema

En `cpu_ciclo.c`, `CPU_INST_MEM_ALLOC` y `CPU_INST_MEM_FREE` están en `es_syscall_o_exit`, así que el ciclo retorna `CPU_CICLO_SYSCALL`. Eso hace que `main.c` ejecute:

```c
guardar_contexto_en_memory(...)
devolver_proceso_a_scheduler(...)  // → envía MSG_DEVOLVER_PROCESO al KS
```

Pero el KS en `manejar_mem_alloc` (hilo separado) hace `km_request`, luego `re_exec_sin_despachar`, y finalmente:

```c
enviar_mensaje(a->fd_cpu, MSG_OK, NULL, 0);
```

El CPU ya salió del ciclo y está en `recibir_proceso_a_ejecutar` esperando `MSG_DESPACHAR_PROCESO` (op_code 17). Recibe `MSG_OK` (op_code 3) y falla con:

```
Mensaje inesperado esperando proceso a ejecutar: op_code=3
```

La CPU corta el loop y se cierra.

### Fix en la CPU (Kevin)

**`cpu/src/cpu_syscalls.c`** — agregar `esperar_ok_kernel` al final de `enviar_syscall_mem_alloc` y `enviar_syscall_mem_free`:

```c
bool enviar_syscall_mem_alloc(...) {
    // ... armar payload y enviar_mensaje ... (sin cambios)
    return esperar_ok_kernel(socket_kernel, "MEM_ALLOC", logger);
    // antes retornaba true directamente
}

bool enviar_syscall_mem_free(...) {
    // ... armar payload y enviar_mensaje ... (sin cambios)
    return esperar_ok_kernel(socket_kernel, "MEM_FREE", logger);
    // antes retornaba true directamente
}
```

**`cpu/src/cpu_ciclo.c`** — sacar `CPU_INST_MEM_ALLOC` y `CPU_INST_MEM_FREE` de `es_syscall_o_exit` y no retornar del ciclo:

```c
static bool es_syscall_o_exit(t_opcode_cpu opcode) {
    return opcode == CPU_INST_MUTEX_CREATE ||
           opcode == CPU_INST_MUTEX_LOCK   ||
           opcode == CPU_INST_MUTEX_UNLOCK ||
           opcode == CPU_INST_SLEEP        ||
           // CPU_INST_MEM_ALLOC  ← SACAR
           // CPU_INST_MEM_FREE   ← SACAR
           opcode == CPU_INST_INIT_PROC    ||
           opcode == CPU_INST_STDOUT       ||
           opcode == CPU_INST_STDIN        ||
           opcode == CPU_INST_EXIT;
}

static bool es_mem_syscall(t_opcode_cpu opcode) {
    return opcode == CPU_INST_MEM_ALLOC || opcode == CPU_INST_MEM_FREE;
}
```

Y en el loop principal de `ejecutar_ciclo_proceso`, agregar el branch para MEM_ALLOC/MEM_FREE antes del bloque de `es_syscall_o_exit`:

```c
// Instrucciones de memoria que NO devuelven el proceso al KS
if (es_mem_syscall(instruccion.opcode)) {
    registros->pc++;
    char parametros[CPU_MAX_PARAMETROS * CPU_MAX_PARAMETRO_LENGTH];
    armar_parametros(&instruccion, parametros, sizeof(parametros));
    log_cpu_ejecucion(logger, pid, opcode_cpu_to_string(instruccion.opcode), parametros);
    if (!ejecutar_syscall(socket_kernel, pid, &instruccion, registros, logger)) {
        return CPU_CICLO_ERROR_EXECUTE;
    }
    // NO retornar — seguir ejecutando el ciclo
    t_interrupcion_cpu interrupcion;
    if (recibir_interrupcion_cpu_si_hay(socket_kernel, &interrupcion, logger)) {
        return CPU_CICLO_INTERRUPCION;
    }
    continue;
}
```

### Fix en el KS (Nicolas)

Con el fix de CPU, el proceso ya no sale de exec durante MEM_ALLOC. El KS puede simplificar `manejar_mem_alloc` y `manejar_mem_free`: sacar las llamadas a `sacar_proc_de_exec_para_mem` y `re_exec_sin_despachar` (que ya no tienen sentido). El proceso se queda en exec durante todo el tiempo que dura la operación de KM.

**`kernel_scheduler/src/main.c`** — en el handler `MSG_MEM_ALLOC`:

```c
case MSG_MEM_ALLOC: {
    // ... validación del payload ... (sin cambios)
    t_payload_syscall_mem_alloc* p = (t_payload_syscall_mem_alloc*) msg->payload;
    int pid_ma = (int)ntohl(p->pid);

    // Buscar el proceso en exec (pero NO sacarlo — la CPU sigue ejecutándolo, bloqueada
    // en esperar_ok_kernel esperando nuestra respuesta)
    t_proceso* proc_ma = NULL;
    pthread_mutex_lock(&mutex_exec);
    for (int i = 0; i < (int)queue_size(cola_exec); i++) {
        t_proceso* q = list_get(cola_exec->elements, i);
        if (q->PID == pid_ma) { proc_ma = q; break; }
    }
    pthread_mutex_unlock(&mutex_exec);

    if (!proc_ma) {
        enviar_mensaje(fd, MSG_ERROR, NULL, 0);
        free_mensaje(msg);
        break;
    }

    t_args_mem_alloc* args_ma = malloc(sizeof(t_args_mem_alloc));
    args_ma->proc        = proc_ma;
    args_ma->fd_cpu      = fd;
    args_ma->id_segmento = ntohl(p->id_segmento);
    args_ma->tamanio     = ntohl(p->tamanio);

    pthread_t t_ma;
    pthread_create(&t_ma, NULL, manejar_mem_alloc, args_ma);
    pthread_detach(t_ma);

    free_mensaje(msg);
    break;
}
```

Y en `manejar_mem_alloc`, sacar las llamadas a `sacar_proc_de_exec_para_mem` y `re_exec_sin_despachar`:

```c
static void* manejar_mem_alloc(void* arg) {
    t_args_mem_alloc* a = (t_args_mem_alloc*) arg;

    t_payload_crear_segmento payload = {
        .pid         = htonl((uint32_t)a->proc->PID),
        .id_segmento = htonl(a->id_segmento),
        .tamanio     = htonl(a->tamanio)
    };

    t_mensaje* resp = km_request(MSG_CREAR_SEGMENTO, &payload, sizeof(payload));

    // El proceso sigue en cola_exec — solo respondemos OK/ERROR a la CPU
    if (resp && resp->op_code == MSG_TABLA_SEGMENTOS) {
        enviar_mensaje(a->fd_cpu, MSG_OK, NULL, 0);
    } else {
        enviar_mensaje(a->fd_cpu, MSG_ERROR, NULL, 0);
    }

    if (resp) free_mensaje(resp);
    free(a);
    return NULL;
}
```

Ídem para `manejar_mem_free`.

**Nota:** el hilo separado sigue siendo necesario porque si KM necesita compactar antes de crear el segmento, enviará `MSG_COMPACTAR` al KS (recibido por `thread_km_listener`), que a su vez llama a `handle_compactar` (también en hilo separado). `handle_compactar` llama `km_request(MSG_FIN_COMPACTACION, ...)`. Si `manejar_mem_alloc` fuera inline en `atender_cpu`, tendría `mutex_km_req` tomado y `handle_compactar` no podría tomarlo → deadlock. El diseño con hilo separado evita exactamente eso.

---

## Bug 3 — CPU conecta a un solo Memory Stick (MEDIO)

**Responsable:** Kevin Castillo / Juan Manuel Fernandez  
**Archivo:** `cpu/src/main.c`

### El problema

La CPU lee `IP_MEMORY_STICK` y `PUERTO_MEMORY_STICK` del config y conecta a **un único MS**. Si el sistema tiene múltiples MS (cada uno con su propio puerto), la CPU solo accede al primero. Las instrucciones `MOV_IN`/`MOV_OUT` sobre datos que físicamente están en el segundo MS fallarán o accederán a direcciones incorrectas.

### Evaluación de impacto

Para la entrega final, si los correctors levantan un único Memory Stick (configuración mínima), esto no se nota. Si usan dos, los datos del segundo MS serán inaccesibles desde la CPU. La prioridad depende de cuántos MS use la corrección.

### Fix posible

Cambiar el config para que acepte una lista de Memory Sticks y la CPU conecte a todos al inicio. Requiere:
- Parsear `MEMORY_STICKS=[127.0.0.1:8003, 127.0.0.1:8004]` en `main.c`
- Guardar un array de `socket_memoria_usuario[]` indexado por `id_memory_stick`
- Pasar ese array a `ejecutar_ciclo_proceso` y usarlo en `cpu_memoria.c` para elegir el fd correcto según `traduccion.id_memory_stick`

**Decisión del equipo:** si en la entrega solo se usan tests con 1 MS, este bug puede diferirse. Si se usan 2 o más, es necesario.

---

## Estado global de los módulos (05/07/2026)

| Módulo | Compila | Flujo básico | Memoria física |
|---|---|---|---|
| utils | ✅ | ✅ | — |
| kernel_memory | ✅ | ✅ | ✅ (leer/escribir físico implementado) |
| kernel_scheduler | ✅ | ✅ | ⚠️ MEM_ALLOC/FREE roto (Bug 2) |
| cpu | ✅ | ✅ | ⚠️ MEM_ALLOC/FREE roto (Bug 2), 1 solo MS (Bug 3) |
| memory_stick | ✅ | ✅ (CPU↔MS) | ❌ KM↔MS no funciona (Bug 1) |
| io | ✅ | ✅ | — |
| swap | ✅ | ✅ | — |

---

## Plan de trabajo sugerido

| Orden | Quién | Qué | Estimación |
|---|---|---|---|
| 1 | Juan Manuel | Bug 1: agregar `atender_km` thread en Memory Stick | ~1h |
| 2 | Kevin | Bug 2 (CPU): `esperar_ok_kernel` en mem_alloc/free + sacar de `es_syscall_o_exit` | ~1h |
| 3 | Nicolas | Bug 2 (KS): simplificar `manejar_mem_alloc`/`manejar_mem_free` para no usar `sacar_proc_de_exec_para_mem` | ~30 min |
| 4 | Todos | Prueba de integración con script que incluya MEM_ALLOC + MOV_IN + STDOUT | ~2h |
| 5 | Kevin/JuanMa | Bug 3: multi-MS en CPU (según necesidad de la corrección) | ~2h |

El orden importa: el Bug 1 y el Bug 2 son independientes entre sí (módulos distintos) — se pueden resolver en paralelo.

---

## Estado de Issues en GitHub (revisión 06/07/2026)

Hay 31 issues abiertos. Se revisó el código en `develop` para verificar cuáles están efectivamente implementados.

### Issues listos para CERRAR (23)

Código verificado — funcionan correctamente:

| # | Título | Evidencia |
|---|---|---|
| #26 | MMU: traducción lógica→física | `cpu_mmu.c`: `id_seg = dir / SEGMENT_MAX_SIZE`, `desp = dir % SEGMENT_MAX_SIZE` ✅ |
| #27 | MOV_IN / MOV_OUT | `cpu_ciclo.c` + `cpu_memoria.c`: `memoria_read`/`memoria_write` implementados ✅ |
| #28 | COPY_MEM | `cpu_ciclo.c`: copia memoria→registro→memoria vía MMU ✅ |
| #30 | INIT_PROC / EXIT | `cpu_syscalls.c`: `enviar_syscall_init_proc` y `enviar_syscall_exit` implementados ✅ |
| #31 | CPU↔MS comunicación directa | `cpu/main.c`: conecta a MS, envía `MSG_CPU_IDENTIFICACION`, recibe `MSG_OK` ✅ |
| #32 | Tabla de segmentos por PID | `kernel_memory/main.c`: `t_tabla_segmentos` por PID con `t_list* segmentos` ✅ |
| #33 | Best Fit | `kernel_memory/main.c`: `buscar_hueco_best_fit()` implementado ✅ |
| #34 | Worst Fit | `kernel_memory/main.c`: `buscar_hueco_worst_fit()` implementado ✅ |
| #35 | Creación/eliminación de segmentos | `MSG_CREAR_SEGMENTO` y `MSG_ELIMINAR_SEGMENTO` en KM ✅ |
| #37 | Hot-plug de Memory Sticks | `MSG_MAS_MEMORIA` en KM: agrega MS a lista con `offset_global` acumulado ✅ |
| #38 | Compactación | `handle_compactar` en KS + `compactar()` en KM con `MSG_FIN_COMPACTACION` ✅ |
| #39 | Suspensión (KM) | `suspender_proceso()`: copia segmentos a Swap con `MSG_SWAP_ESCRIBIR` ✅ |
| #40 | Des-suspensión (KM) | `dessuspender_proceso()`: restaura desde Swap con `MSG_SWAP_LEER` ✅ |
| #42 | Des-suspensión por memoria disponible (KS) | `manejar_mas_memoria()`: intenta des-suspender procesos en SUSPENDED ✅ |
| #47 | BSOD por desconexión de MS | `manejar_bsod()`: finaliza todos los procesos activos ✅ |
| #49 | Swap lectura/escritura | `swap/main.c`: `MSG_SWAP_LEER` y `MSG_SWAP_ESCRIBIR` implementados ✅ |
| #53 | Logs obligatorios KM | Todos los logs requeridos presentes con formato correcto ✅ |
| #62 | Syscalls MUTEX_CREATE/UNLOCK/EXIT no esperan MSG_OK | `cpu_syscalls.c`: `return true` directo en esas tres, sin `esperar_ok_kernel` ✅ |
| #64 | STDOUT/STDIN con dirección física | KM recibe dirección lógica de KS y hace `traducir_direccion()` internamente ✅ |
| #65 | Flujo completo STDOUT | KS→KM con `MSG_LEER_DATOS`, KM→MS, KM→KS con `MSG_LEER_DATOS_RESP`, KS→IO ✅ |
| #66 | Flujo completo STDIN | KS→IO, IO→KS con datos, KS→KM con `MSG_ESCRIBIR_DATOS` ✅ |
| #68 | Des-suspensión usa BEST/WORST FIT | `dessuspender_proceso()` llama `buscar_hueco_best_fit`/`worst_fit` según config ✅ |
| #69 | Direcciones físicas globales | `ms_para_direccion()` divide por rango con `offset_global` por MS ✅ |

### Issues que deben PERMANECER ABIERTOS (8)

#### Bugs de código real

| # | Título | Problema |
|---|---|---|
| #36 | KM lectura/escritura desde/hacia MS | MS no lee de `fd_km` después del handshake → KM se bloquea en `ms_leer`/`ms_escribir` (Bug 1) |
| #29 | MEM_ALLOC/MEM_FREE en CPU | CPU retorna `CPU_CICLO_SYSCALL` → envía `MSG_DEVOLVER_PROCESO` en lugar de esperar `MSG_OK` (Bug 2, lado CPU) |
| #67 | MEM_ALLOC/MEM_FREE re-despacho KS | KS hace `sacar_proc_de_exec` + `km_request` + `re_exec` + `MSG_OK` → incompatible con el flujo corregido (Bug 2, lado KS) |

#### Logs con discrepancias reales (#52 y #54)

**#52 — KS logs faltantes:**

El handler `MSG_MEM_ALLOC` en `atender_cpu` (línea ~1086 de `kernel_scheduler/src/main.c`) loguea `## (%d) - MEM_ALLOC: solicita crear segmento...` en vez del formato estándar. Faltan los logs:

```
## (<PID>) - Solicitó syscall: MEM_ALLOC    ← ausente en handler de MSG_MEM_ALLOC
## (<PID>) - Solicitó syscall: MEM_FREE     ← ausente en handler de MSG_MEM_FREE
## (<PID>) - Solicitó syscall: INIT_PROC    ← ausente en handler de MSG_INIT_PROC
```

Responsable del fix: **Nicolas** (MEM_ALLOC, MEM_FREE — mismo archivo que Bug 2) + **Kevin** (INIT_PROC — `atender_cpu` lo maneja en el hilo de CPU).

**#54 — CPU logs con acentos incorrectos:**

En `cpu/src/cpu_logs.c`:
- Línea 8: `"## Interrupcion recibida"` → debería ser `"## Interrupción recibida"`
- Línea 18: `"PID: %u - Accion: %s - Direccion Fisica: %u - Valor: %u"` → debería ser `"Acción"` y `"Dirección Física"`

Responsable del fix: **Kevin** (archivos de CPU).

#### Testing / deployment sin ejecutar

| # | Título | Estado |
|---|---|---|
| #50 | Prueba de integración multi-CPU | No ejecutada — requiere tener bugs 1 y 2 resueltos |
| #51 | Prueba de hot-plug dinámico | No ejecutada — requiere levantar sistema completo |
| #55 | Script de deployment | No implementado |

### Resumen ejecutivo

```
CERRAR ahora (23):  #26 #27 #28 #30 #31 #32 #33 #34 #35 #37
                    #38 #39 #40 #42 #47 #49 #53 #62 #64 #65
                    #66 #68 #69

MANTENER abiertos (8): #29 #36 #67 (bugs)
                       #52 #54 (logs)
                       #50 #51 #55 (testing/deploy)
```
