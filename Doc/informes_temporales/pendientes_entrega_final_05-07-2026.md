# Pendientes para la entrega final — 05/07/2026 (actualizado 06/07/2026)

**Fecha límite:** 11/07/2026  
**Elaborado por:** Nicolas Alessandro Barreiro  
**Base:** revisión directa del código en `develop` — no de la documentación

---

## Historial de cambios del documento

| Fecha | Cambio |
|---|---|
| 05/07/2026 | Versión inicial: identificados Bug 1, Bug 2, Bug 3 + análisis de issues |
| 06/07/2026 | Merge de `fix/ks-bugs-runtime` a `develop`. Bug 1 y Bug 2 resueltos. Nuevos bugs identificados y corregidos en el mismo merge. |

---

## Estado actual (06/07/2026)

El merge de `fix/ks-bugs-runtime` resolvió **todos los bugs críticos de runtime** identificados ayer y varios bugs adicionales que no habíamos detectado. El sistema ahora puede ejecutar scripts con memoria física (MEM_ALLOC, MOV_IN/OUT, STDOUT/STDIN real).

### Resuelto en el merge (06/07/2026)

| Bug | Módulo | Descripción | Responsable |
|---|---|---|---|
| Bug 1 | MS | MS no tenía hilo que leyera `fd_km` → KM se bloqueaba en `ms_leer`/`ms_escribir` | Juan Manuel |
| Bug 2 (KS) | KS | `manejar_mem_alloc`/`manejar_mem_free`: flujo incompatible con la CPU. Fix: `consumir_devolver()` + `redespachar_a_misma_cpu()` | Santiago |
| Bug 2 (CPU) | CPU | `recibir_proceso_a_ejecutar` abortaba ante mensajes inesperados (interrupciones tardías). Fix: `continue` en lugar de `return false` | Santiago |
| KM deadlock compactación | KM | El hilo que atendía `MSG_CREAR_SEGMENTO` hacía `sem_wait` esperando `MSG_FIN_COMPACTACION`, pero ambos llegan por la misma conexión → deadlock determinístico. Fix: pedido pendiente + responder desde el handler de `MSG_FIN_COMPACTACION` | Santiago |
| KS deadlock cruzado compactación | KS | `handle_compactar` no podía usar `km_request` (el mutex lo tenía `manejar_mem_alloc`). Fix: `mutex_km_send` para envío directo + `sem_fin_compactacion` | Santiago |
| Comparación de prioridades invertida | KS | `cmp_suspension` usaba `>` en lugar de `<` → orden de des-suspensión por prioridad al revés | Santiago |
| Quantum timer fantasma | KS | Timer de RR podía desalojar un proceso ya re-despachado. Fix: campo `gen_despacho` en `t_proceso` | Santiago |
| CPU desconectada durante EXEC | KS | El proceso en ejecución quedaba huérfano. Fix: rescate del proceso + vuelta a READY | Santiago |
| IO desconectada | KS | `fd_io_*` no se invalidaba → próximas syscalls enviaban a socket muerto | Santiago |
| Lector competitivo MS/Swap en KM | KM | Tras identificarse, el hilo de aceptación seguía leyendo en competencia con `ms_leer`/`ms_escribir`. Fix: `conexion_pasiva = 1` → hilo termina tras identificación | Santiago |
| KS logs MEM_ALLOC/MEM_FREE | KS | Faltaban `"## (%d) - Solicitó syscall: MEM_ALLOC/MEM_FREE"` | Santiago |
| MUTEX_LOCK sobre mutex inexistente | KS | Proceso quedaba fuera de todas las colas. Fix: finalizar con motivo ERROR | Santiago |
| Des-suspensión enviaba MSG_COMPACTAR | KM | Cuando no había espacio, KM enviaba `MSG_COMPACTAR` al intentar des-suspender. Fix: `MSG_ERROR` (el enunciado prohíbe compactar en des-suspensión) | Santiago |

---

## Pendientes reales (post-merge)

### ~~Pendiente 1~~ — CPU multi-Memory Stick ✅ RESUELTO (06/07/2026)

**Commit:** `8565a44`  
**Informe:** `cpu/informes_temporales/feat_multi_ms_06-07-2026.md`

Array `sockets_ms[CPU_MAX_MS]` indexado por `id_memory_stick`. Config usa claves `IP_MEMORY_STICK_0`, `IP_MEMORY_STICK_1`, etc. con fallback a `IP_MEMORY_STICK` (sin índice) para compatibilidad. `COPY_MEM` selecciona fd correcto para origen y destino por separado.

---

### ~~Pendiente 2~~ — Log "Solicitó syscall: INIT_PROC" ✅ RESUELTO (06/07/2026)

**Commit:** `1ed8a79`  
**Informe:** `kernel_scheduler/informes_temporales/fix_log_init_proc_06-07-2026.md`

Se extrajo `pid_padre` del payload y se agregó `log_info(logger, "## (%d) - Solicitó syscall: INIT_PROC", pid_padre)` antes de `crear_proceso` en el handler `MSG_INIT_PROC`.

---

### ~~Pendiente 3~~ — Logs CPU con acentos incorrectos ✅ RESUELTO (06/07/2026)

**Commit:** `2308fc4`  
**Informe:** `cpu/informes_temporales/fix_logs_acentos_06-07-2026.md`

Corregidos los tres strings en `cpu_logs.c`: `"Interrupcion"` → `"Interrupción"`, `"Accion"` → `"Acción"`, `"Direccion Fisica"` → `"Dirección Física"`.

---

## Estado global de los módulos (06/07/2026)

| Módulo | Compila | Flujo básico | Memoria física | Observaciones |
|---|---|---|---|---|
| utils | ✅ | ✅ | — | — |
| kernel_memory | ✅ | ✅ | ✅ | Deadlock de compactación resuelto |
| kernel_scheduler | ✅ | ✅ | ✅ | Todos los bugs críticos resueltos |
| cpu | ✅ | ✅ | ✅ | Multi-MS implementado. Logs corregidos |
| memory_stick | ✅ | ✅ | ✅ | Hilo `atender_kernel_memory` agregado |
| io | ✅ | ✅ | — | — |
| swap | ✅ | ✅ | — | — |

---

## Plan de trabajo restante

| Orden | Quién | Qué | Estimación |
|---|---|---|---|
| 1 | Kevin | Agregar `## (%d) - Solicitó syscall: INIT_PROC` en handler MSG_INIT_PROC | 5 min |
| 2 | Kevin | Corregir acentos en `cpu_logs.c` (3 strings) | 5 min |
| 3 | Todos | Prueba de integración con script que use MEM_ALLOC + MOV_IN/OUT + STDOUT | ~2h |
| 4 | Kevin/JuanMa | Bug 3: multi-MS en CPU (según necesidad de la corrección) | ~2h |
| 5 | Nicolas | Merge `develop → main` para entrega final | 15 min |

---

## Estado de Issues en GitHub (revisión 06/07/2026)

### Issues listos para CERRAR (23)

Código verificado en `develop` post-merge:

| # | Título | Evidencia |
|---|---|---|
| #26 | MMU: traducción lógica→física | `cpu_mmu.c` ✅ |
| #27 | MOV_IN / MOV_OUT | `cpu_ciclo.c` + `cpu_memoria.c` ✅ |
| #28 | COPY_MEM | `cpu_ciclo.c` ✅ |
| #30 | INIT_PROC / EXIT | `cpu_syscalls.c` ✅ |
| #31 | CPU↔MS comunicación directa | `cpu/main.c` + `memory_stick/main.c` ✅ |
| #32 | Tabla de segmentos por PID | `kernel_memory/main.c` ✅ |
| #33 | Best Fit | `buscar_hueco_best_fit()` ✅ |
| #34 | Worst Fit | `buscar_hueco_worst_fit()` ✅ |
| #35 | Creación/eliminación de segmentos | `MSG_CREAR_SEGMENTO` / `MSG_ELIMINAR_SEGMENTO` ✅ |
| #36 | KM lectura/escritura desde/hacia MS | `ms_leer()`/`ms_escribir()` + hilo `atender_kernel_memory` en MS ✅ |
| #37 | Hot-plug de Memory Sticks | `MSG_MAS_MEMORIA` + `offset_global` ✅ |
| #38 | Compactación | `handle_compactar` KS + `compactar()` KM — deadlock resuelto ✅ |
| #39 | Suspensión | `suspender_proceso()` → Swap ✅ |
| #40 | Des-suspensión | `dessuspender_proceso()` desde Swap ✅ |
| #42 | Des-suspensión por memoria disponible | `manejar_mas_memoria()` ✅ |
| #47 | BSOD | `manejar_bsod()` ✅ |
| #49 | Swap lectura/escritura | `MSG_SWAP_LEER` / `MSG_SWAP_ESCRIBIR` ✅ |
| #53 | Logs KM | Todos los logs requeridos presentes ✅ |
| #62 | Syscalls MUTEX_CREATE/UNLOCK/EXIT no esperan MSG_OK | `cpu_syscalls.c` ✅ |
| #64 | STDOUT/STDIN con dirección física | KM traduce internamente ✅ |
| #65 | Flujo completo STDOUT | KS→KM→MS→IO ✅ |
| #66 | Flujo completo STDIN | IO→KS→KM→MS ✅ |
| #67 | MEM_ALLOC/MEM_FREE re-despacho a misma CPU | `redespachar_a_misma_cpu()` ✅ |
| #68 | Des-suspensión usa BEST/WORST FIT | `dessuspender_proceso()` ✅ |
| #69 | Direcciones físicas globales | `ms_para_direccion()` + `offset_global` ✅ |
| #29 | MEM_ALLOC/MEM_FREE en CPU | `consumir_devolver()` + `recibir_proceso_a_ejecutar()` resiliente ✅ |

### Issues que deben PERMANECER ABIERTOS (5)

| # | Título | Razón |
|---|---|---|
| #52 | Logs KS | ✅ Resuelto — commit `1ed8a79` |
| #54 | Logs CPU/IO/MS/Swap | ✅ Resuelto — commit `2308fc4` |
| #50 | Prueba multi-CPU | No ejecutada aún |
| #51 | Prueba hot-plug dinámico | No ejecutada aún |
| #55 | Script de deployment | No implementado |

### Resumen ejecutivo

```
CERRAR ahora (26):  #26 #27 #28 #29 #30 #31 #32 #33 #34 #35
                    #36 #37 #38 #39 #40 #42 #47 #49 #53 #62
                    #64 #65 #66 #67 #68 #69

MANTENER abiertos (5): #52 #54 (logs menores)
                       #50 #51 #55 (testing/deploy)
```
