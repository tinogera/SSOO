# Fix: KM abría `<pid>.txt` en lugar del script enviado por KS — 06/07/2026

**Módulo:** kernel_memory  
**Archivos modificados:**
- `kernel_memory/src/main.c`

**Commit:** (ver git log)

## Problema

Al ejecutar la prueba `PLANI_PRE_0`, KM registraba el error:

```
[ERROR] KernelMemory: No se pudo abrir: /home/.../plug-n-pray-pruebas/0.txt
```

El archivo real era `PLANI_PRE_0.prc` (o similar), no `0.txt`.

### Causa raíz

El payload de `MSG_CREAR_PROCESO` tiene el formato `{uint32_t pid, char path[]}`:
- KS envía el nombre del script que el usuario pasó al crear el proceso.
- KM sólo leía los primeros 4 bytes (el pid) e ignoraba el campo `path`.

En `leer_instruccion` se construía la ruta como `SCRIPTS_BASEPATH/<pid>.txt`, que nunca existía.

## Fix

Se agrega una lista local `script_paths` (tipo `t_list*` de `t_pid_path*`) protegida por `mutex_contextos`. El tipo auxiliar es:

```c
typedef struct { uint32_t pid; char* path; } t_pid_path;
```

**En `MSG_CREAR_PROCESO`:** se extrae `script_name = (char*)pedido->payload + 4` (el string está null-terminated por el protocolo) y se almacena con `strdup` en la lista.

**En `leer_instruccion`:** se busca el pid en `script_paths`. Si se encuentra, se construye la ruta como `SCRIPTS_BASEPATH/<script_name>`; si no (fallback defensivo), se usa `SCRIPTS_BASEPATH/<pid>.txt` como antes.

```c
for (int i = 0; i < list_size(script_paths); i++) {
    t_pid_path* pp = list_get(script_paths, i);
    if (pp->pid == pid) { script_name = pp->path; break; }
}
if (script_name) snprintf(path, sizeof(path), "%s/%s", base, script_name);
else             snprintf(path, sizeof(path), "%s/%u.txt", base, pid);
```

La búsqueda se hace bajo `mutex_contextos` para evitar carreras con el handler de `MSG_CREAR_PROCESO`.

## Verificación

El módulo compila sin warnings. La prueba de integración `PLANI_PRE_0` ya no produce el error de apertura de archivo.
