# Planificación Individual — Luciano Lisachi

## Módulos Asignados
- **Principal:** Kernel Memory — Segmentación pura, algoritmos de asignación, hot-plug, compactación, suspensión/des-suspensión
- **Secundario:** Swap — Implementación completa

## Ramas de trabajo

| Rama | Tareas |
|---|---|
| `feature/kernel-memory/conexiones-instrucciones` | Servidor de sockets KMemory, instrucciones, contextos (en conjunto con Kevin) |
| `feature/kernel-memory/segmentacion` | Tabla de segmentos, Best/Worst Fit, creación/eliminación, lectura/escritura de datos, hot-plug |
| `feature/kernel-memory/suspension-compactacion` | Compactación, suspensión a Swap, des-suspensión desde Swap |
| `feature/swap` | Conexión a KMemory, lectura/escritura de bloques |

> Crear `feature/kernel-memory/conexiones-instrucciones` desde `develop` al iniciar Fase 1 (coordinar con Kevin). Las demás ramas se crean desde `develop` al inicio de Fase 3.

---

## Fase 0 — Configuración del Entorno
**Fecha límite:** 13/04/2026 — **COMPLETADA**

- [x] Instalar `so-commons-library` y verificar compilación de `kernel_memory` y `swap`.
- [x] Leer y entender completamente la sección de Kernel Memory y Swap en la consigna: segmentación pura, gestión de huecos, hot-plug, compactación.
- [x] Coordinar con Nicolas el diseño del protocolo de mensajes KMemory↔Scheduler, KMemory↔CPU, KMemory↔Memory Sticks y KMemory↔Swap.

---

## Fase 1 — Check 1: Conexiones
**Fecha límite:** 18/04/2026 — **ENTREGADO (parcial, completado 26/04)**
**Ramas:** `feature/kernel-memory/conexiones-instrucciones` · `feature/swap`

- [x] Implementar el **servidor de sockets** en Kernel Memory:
  - Conexión desde **Kernel Scheduler**.
  - Conexiones dinámicas de **CPUs** (multihilo).
  - Conexiones dinámicas de **Memory Sticks** (hot-plug).
  - Conexión de **Swap**.
- [x] Implementar la **conexión de Swap** a Kernel Memory: enviar capacidad total al conectarse.
- [x] Logear las conexiones establecidas:
  - `## Kernel Scheduler Conectado - FD del socket: <FD>`.
  - `## CPU <ID CPU> Conectada`.
  - `## Memory Stick de <TAMAÑO> bytes Conectada`.
  - `## Conectado a Kernel Memory` (desde Swap).

**Dependencias:** Coordinarse con Nicolas para usar el wrapper de sockets de Utils.

---

## Fase 2 — Check 2: Instrucciones y Contextos Mock
**Fecha límite:** 23/05/2026 — **COMPLETADA** (merge a main el 22/05/2026, commit `29f8611`)
**Rama:** `feature/kernel-memory/conexiones-instrucciones`

### Semana 1–2 (19/04 – 02/05)
- [x] Implementar la **lectura de archivos de pseudocódigo**: dado el `SCRIPTS_BASEPATH` y el path del proceso, leer el archivo y retornar la línea correspondiente al PC recibido.
- [x] Aplicar el **INSTRUCTION_DELAY** antes de responder.
- [x] Implementar log: `## PID: <PID> - Obtener instrucción: <PC> - Instrucción: <INSTRUCCIÓN> <...ARGS>`.

### Semana 3–5 (03/05 – 23/05)
- [x] Implementar la **creación de proceso** en Kernel Memory: recibir PID + path de instrucciones desde Scheduler, inicializar estructura de contexto con todos los registros en 0.
- [x] Implementar **guardar y restaurar contexto** (registros + tabla de segmentos) por PID (versión mock sin segmentación real).
- [x] Implementar log: `## PID: <PID> - Proceso Creado`.

---

## Fase 3 — Check 3: Segmentación Completa
**Fecha límite:** 20/06/2026
**Ramas:** `feature/kernel-memory/segmentacion` (semanas 1–3) · `feature/kernel-memory/suspension-compactacion` (semana 4) · `feature/swap` (semanas 3–4)

### Semana 1 (24/05 – 30/05)
- [ ] Diseñar e implementar la **tabla de segmentos** por PID: cada entrada contiene ID de segmento, Memory Stick de destino, dirección base física, límite.
- [ ] Implementar la estructura de **mapa de memoria libre**: lista de huecos (dirección de inicio, tamaño) por cada Memory Stick.

### Semana 2 (31/05 – 06/06)
- [ ] Implementar algoritmo **Best Fit**: al crear un segmento, seleccionar el hueco libre más pequeño que lo contenga.
- [ ] Implementar algoritmo **Worst Fit**: al crear un segmento, seleccionar el hueco libre más grande disponible.
- [ ] Implementar **creación de segmento** (MEM_ALLOC): recibir PID + ID segmento + tamaño, aplicar algoritmo de selección, actualizar tabla de segmentos y mapa de huecos.
- [ ] Implementar **eliminación de segmento** (MEM_FREE): recibir PID + ID segmento, liberar espacio y actualizar tabla.
- [ ] Implementar log: `## PID: <PID> - Segmento Creado <ID_SEGMENTO> - Tamaño: <TAMAÑO>`.

### Semana 3 (07/06 – 13/06)
- [ ] Implementar **lectura de datos**: recibir PID + dirección lógica + tamaño, traducir a dirección física usando tabla de segmentos, solicitar datos al Memory Stick correspondiente, retornar al solicitante.
- [ ] Implementar **escritura de datos**: recibir PID + dirección lógica + tamaño + datos, traducir a física, enviar al Memory Stick.
- [ ] Manejar casos donde la operación abarca múltiples Memory Sticks (dividir y consolidar).
- [ ] Implementar log: `## PID: <PID> - <Escritura/Lectura> - Dir. Física: <DIRECCIÓN_FÍSICA> - Tamaño: <TAMAÑO>`.

### Semana 4 (14/06 – 20/06)
- [ ] Implementar **hot-plug de Memory Sticks**: al conectarse un nuevo stick, agregar su espacio al mapa de memoria libre, notificar al Kernel Scheduler que hay más memoria disponible.
- [ ] Implementar **compactación**: desplazar segmentos para eliminar fragmentación, actualizar tabla de segmentos de todos los PIDs afectados, aplicar `COMPACTION_DELAY`.
- [ ] Implementar **suspensión de proceso**: para cada segmento del PID, solicitar bloques libres a Swap, copiar datos al Swap, liberar espacio en Memory Sticks.
- [ ] Implementar **des-suspensión de proceso**: recuperar segmentos desde Swap, reasignar espacio en Memory Sticks, restaurar tabla de segmentos del PID.

### Ajustes por v1.1 del enunciado (08/06/2026)

- [ ] **Des-suspensión usa algoritmo de búsqueda de huecos**: al restaurar segmentos de SWAP, aplicar BEST FIT o WORST FIT (según `FITTING_ALGORITHM` config) para ubicar cada segmento. No asignar en posición arbitraria.

- [ ] **Direcciones físicas son globales, no relativas a cada Memory Stick**:
  - El KM asigna direcciones físicas únicas y globales a cada segmento (no reinicia en 0 por cada stick).
  - Llevar un mapa global de rangos: qué rango de dirección física corresponde a cada MS (p. ej., MS1: 0–255, MS2: 256–511).
  - Al atender una lectura o escritura con dirección física, calcular a qué MS(s) pertenece y, si cruza fronteras, dividir la operación.
  - Coordinar con Juan Manuel el nuevo formato del protocolo KM↔MS (ya no se asume offset 0).

- [ ] **Implementar lectura/escritura desde KS** (flujo STDOUT/STDIN completo):
  - Agregar handlers en KM para `MSG_KM_LEER_MEMORIA` (devolver bytes) y `MSG_KM_ESCRIBIR_MEMORIA` (escribir bytes en dir física).
  - Estos mensajes llegan del KS, no de la CPU.

### Swap — Semana 3–4 (07/06 – 20/06)
- [ ] Implementar el **servidor de Swap**: crear/abrir archivo binario del tamaño configurado (`SWAP_FILE_SIZE`).
- [ ] Implementar **escritura de bloque**: recibir número de bloque + contenido (tamaño = `BLOCK_SIZE`) → escribir en posición `numero_bloque * BLOCK_SIZE` → confirmar.
- [ ] Implementar **lectura de bloque**: recibir número de bloque → leer `BLOCK_SIZE` bytes → retornar.
- [ ] Implementar logs: `## Conectado a Kernel Memory`, `## Escritura del bloque: <N>`, `## Lectura del bloque: <N>`.

---

## Fase 4 — Integración y Entrega Final
**Fecha límite:** 11/07/2026
**Rama:** `develop` (integración directa)

- [ ] Integrar Kernel Memory con Memory Sticks y Swap reales.
- [ ] Testear compactación con múltiples procesos y Memory Sticks.
- [ ] Testear suspensión/des-suspensión con procesos reales.
- [ ] Verificar todos los logs obligatorios de Kernel Memory y Swap.
- [ ] Colaborar en la resolución de bugs de integración.

---

## Interfaces a acordar con otros integrantes

| Con quién | Qué acordar |
|---|---|
| **Kevin** | Formato de mensajes KMemory↔CPU: fetch de instrucción, guardar/restaurar contexto |
| **Juan Manuel** | Protocolo KMemory↔CPU para lectura/escritura de datos (traducción lógica→física), protocolo KMemory↔Memory Stick |
| **Santiago** | Protocolo KMemory↔Scheduler: creación proceso, notificación hot-plug, notificación compactación, BSOD |
