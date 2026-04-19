# Memory Stick — Pendiente Check 1

**Responsable:** Juan Manuel  
**Estado actual:** conecta a Kernel Memory y levanta servidor, pero tiene 4 bugs que impiden compilar o causan segfault en runtime  
**Prioridad:** ALTA — sin estos fixes el módulo no funciona

---

## Resumen de bugs

| # | Línea | Tipo | Descripción |
|---|-------|------|-------------|
| 1 | 19 | Compilación | `kernel_ip` declarado como `char` en vez de `char*` |
| 2 | 98-100 | Runtime / segfault | `snprintf` sobre puntero no inicializado |
| 3 | 103 | Protocolo | Op_code incorrecto (`MSG_IO_IDENTIFICACION` en vez de `MSG_MEMORY_STICK_IDENTIFICACION`) |
| 4 | 115 | Runtime / UB | `fd_cliente` (int) impreso con `%s` |

---

## Bug 1 — Declaración incorrecta de `kernel_ip` (línea 19)

`char* ip, kernel_ip` en C declara `ip` como `char*` pero `kernel_ip` como `char` (un solo byte). Cuando `config_get_string_value` intenta escribir un puntero ahí, corrompe el stack.

```c
// ANTES (línea 18-19)
int puerto, id, delay, kernel_port;
char* ip,kernel_ip;

// DESPUÉS
int puerto, id, delay, kernel_port;
char* ip;
char* kernel_ip;
```

---

## Bug 2 — `snprintf` sobre puntero no inicializado (líneas 98-100)

`msdatos` es un `char*` declarado sin asignar memoria. `snprintf` escribe en una dirección aleatoria → segfault garantizado. Además, el segundo `snprintf` sobreescribe lo que escribió el primero en vez de concatenar.

```c
// ANTES (líneas 98-100)
char* msdatos;
snprintf(msdatos, sizeof(msdatos), "%s", ip);
snprintf(msdatos, sizeof(msdatos), ", %s", puerto);

// DESPUÉS
char msdatos[128];
snprintf(msdatos, sizeof(msdatos), "%s, %d", ip, puerto);
```

---

## Bug 3 — Op_code incorrecto en `enviar_mensaje` (línea 103)

Se está usando `MSG_IO_IDENTIFICACION` — Kernel Memory no va a reconocer que sos un Memory Stick y puede ignorar o rechazar la conexión.

```c
// ANTES (línea 103)
enviar_mensaje(fd, MSG_IO_IDENTIFICACION, payload, size);

// DESPUÉS
enviar_mensaje(fd, MSG_MEMORY_STICK_IDENTIFICACION, payload, size);
```

> `MSG_MEMORY_STICK_IDENTIFICACION` lo agrega Nicolas en `utils/src/utils/protocolo.h`. Coordinar antes de compilar.

---

## Bug 4 — Log imprime fd como string (línea 115)

`fd_cliente` es un `int`. Pasarlo a `%s` es undefined behavior (probablemente segfault o basura en el log).

```c
// ANTES (línea 115)
log_info(logger, "## CPU <%s> Conectada\n", fd_cliente);

// DESPUÉS
log_info(logger, "## CPU Conectada (fd=%d)\n", fd_cliente);
```

---

## Paso a paso para aplicar los fixes

```bash
# Editar el archivo
nano memory_stick/src/main.c
# (o con el editor que uses)

# Aplicar los 4 cambios detallados arriba

# Compilar
cd memory_stick
make
```

Si compila sin errores ni warnings, los bugs 1 y 2 están corregidos.

---

## Cómo probar

Primero levantar Kernel Memory (puerto 37215), luego:

```bash
./bin/memory_stick memory_stick.config [TAMAÑO]
```

En el log de Kernel Memory deberías ver:
```
[INFO] ## Memory Stick Conectado: 127.0.0.1, 37216
```

Y en el log de Memory Stick:
```
[INFO] ## Conectado a Kernel Memory
[INFO] Se levanto servidor para esperar un CPU, puerto: 37216
```

---

## Dependencias

- `MSG_MEMORY_STICK_IDENTIFICACION` debe estar en `utils/src/utils/protocolo.h` antes de compilar. Avisarle a Nicolas.
- Kernel Memory debe estar corriendo antes de levantar Memory Stick.
