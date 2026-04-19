# Kernel Memory — Pendiente Check 1

**Responsable:** Luciano  
**Estado actual:** placeholder (`saludar("kernel_memory")` — no hace nada)  
**Prioridad:** ALTA — todos los otros módulos se conectan a este servidor

---

## Qué hay que hacer

Kernel Memory es un **servidor TCP**. Debe arrancar, abrir un puerto, y aceptar conexiones de:
- Kernel Scheduler (envía `MSG_KS_IDENTIFICACION`)
- CPU (envía `MSG_CPU_IDENTIFICACION`)
- Memory Stick (envía `MSG_MEMORY_STICK_IDENTIFICACION`)
- Swap (envía `MSG_SWAP_IDENTIFICACION`)

A cada uno hay que responderle con `MSG_OK` y loguear quién se conectó.

El módulo de referencia para copiar el patrón es `kernel_scheduler/src/main.c` — ya está implementado y funciona igual.

---

## Paso 1 — Agregar dependencia de pthread en el Makefile

Verificar que en `settings.mk` o `Makefile` esté `-lpthread` en los flags. Si no está, agregarlo. El Kernel Scheduler ya lo tiene — copiar la misma línea.

---

## Paso 2 — Reemplazar `kernel_memory/src/main.c`

```c
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <commons/log.h>
#include <commons/config.h>
#include <utils/sockets.h>
#include <utils/protocolo.h>

t_log* logger;

void* atender_cliente(void* arg);

int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Uso: %s [PUERTO_ESCUCHA]\n", argv[0]);
        return EXIT_FAILURE;
    }

    int puerto = atoi(argv[1]);

    logger = log_create("kernel_memory.log", "KernelMemory", true, LOG_LEVEL_INFO);
    if (logger == NULL) {
        fprintf(stderr, "Error al crear el logger\n");
        return EXIT_FAILURE;
    }

    int fd_servidor = crear_servidor(puerto);
    if (fd_servidor < 0) {
        log_error(logger, "Error al crear servidor en puerto %d", puerto);
        return EXIT_FAILURE;
    }

    log_info(logger, "Servidor levantado en puerto %d", puerto);

    while (1) {
        int fd_cliente = aceptar_conexion(fd_servidor);

        int* fd_mem = malloc(sizeof(int));
        *fd_mem = fd_cliente;

        pthread_t hilo;
        pthread_create(&hilo, NULL, atender_cliente, fd_mem);
        pthread_detach(hilo);
    }

    return 0;
}

void* atender_cliente(void* arg) {
    int fd = *((int*)arg);
    free(arg);

    t_mensaje* msg = recibir_mensaje(fd);
    if (msg == NULL) return NULL;

    if (msg->op_code == MSG_KS_IDENTIFICACION) {
        log_info(logger, "## Kernel Scheduler Conectado");
        enviar_mensaje(fd, MSG_OK, NULL, 0);
    }
    else if (msg->op_code == MSG_CPU_IDENTIFICACION) {
        char* id = deserializar_string(msg->payload);
        log_info(logger, "## CPU %s Conectada", id);
        free(id);
        enviar_mensaje(fd, MSG_OK, NULL, 0);
    }
    else if (msg->op_code == MSG_MEMORY_STICK_IDENTIFICACION) {
        char* datos = deserializar_string(msg->payload);
        log_info(logger, "## Memory Stick Conectado: %s", datos);
        free(datos);
        enviar_mensaje(fd, MSG_OK, NULL, 0);
    }
    else if (msg->op_code == MSG_SWAP_IDENTIFICACION) {
        log_info(logger, "## Swap Conectado");
        enviar_mensaje(fd, MSG_OK, NULL, 0);
    }

    free_mensaje(msg);
    return NULL;
}
```

---

## Paso 3 — Crear `kernel_memory/kernel_memory.config`

```
PUERTO_ESCUCHA=37215
LOG_LEVEL=INFO
```

> El puerto 37215 ya está hardcodeado en `cpu/cpu.config` como `PUERTO_MEMORY`. No cambiar.

---

## Paso 4 — Compilar y probar

```bash
# Desde la raíz del repo
cd kernel_memory
make

# Levantar el servidor
./bin/kernel_memory 37215
```

Deberías ver en la consola:
```
[INFO] Servidor levantado en puerto 37215
```

Y al conectarse cada módulo, los logs correspondientes:
```
[INFO] ## Kernel Scheduler Conectado
[INFO] ## CPU cpu_0 Conectada
[INFO] ## Memory Stick Conectado: 127.0.0.1, 37216
[INFO] ## Swap Conectado
```

---

## Dependencias con otros módulos

- **Utils (`protocolo.h`)**: necesita que estén definidos `MSG_MEMORY_STICK_IDENTIFICACION` y `MSG_SWAP_IDENTIFICACION`. Nicolas los agrega — coordinar antes de compilar.
- **Kernel Memory debe arrancar primero** que todos los demás módulos.

---

## Orden de arranque para prueba integral

```
1. kernel_memory   (este módulo)
2. kernel_scheduler
3. memory_stick
4. swap
5. cpu
6. io
```
