# Fix: acentos faltantes en strings de log — 06/07/2026

**Módulo:** CPU  
**Archivo:** `cpu/src/cpu_logs.c`  
**Commit:** `2308fc4`

## Problema

Tres strings de log en `cpu_logs.c` usaban palabras sin acento, incompatibles con el formato obligatorio del enunciado:

| Función | String actual (incorrecto) | String requerido |
|---|---|---|
| `log_cpu_interrupcion` | `"## Interrupcion recibida"` | `"## Interrupción recibida"` |
| `log_cpu_acceso_memoria` | `"PID: %u - Accion: %s - ..."` | `"PID: %u - Acción: %s - ..."` |
| `log_cpu_acceso_memoria` | `"... - Direccion Fisica: %u - ..."` | `"... - Dirección Física: %u - ..."` |

Si los correctores verifican los logs por matching exacto de strings, estos tres fallarían.

## Fix aplicado

Reemplazo directo de los tres strings en `cpu_logs.c`:

```c
// Antes:
log_info(logger, "## Interrupcion recibida");
// Después:
log_info(logger, "## Interrupción recibida");

// Antes:
"PID: %u - Accion: %s - Direccion Fisica: %u - Valor: %u"
// Después:
"PID: %u - Acción: %s - Dirección Física: %u - Valor: %u"
```

## Verificación

El módulo CPU compila sin warnings. Los strings con UTF-8 son válidos en C — GCC los trata como secuencias de bytes literales, sin implicancias de runtime.
