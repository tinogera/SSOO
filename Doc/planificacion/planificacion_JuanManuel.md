# Planificación Individual — Juan Manuel Fernandez Vazquez

## Módulos Asignados
- **Principal:** CPU — MMU, instrucciones de acceso a memoria, syscalls de memoria
- **Secundario:** Memory Stick — Implementación completa

## Ramas de trabajo

| Rama | Tareas |
|---|---|
| `feature/memory-stick` | Servidor de sockets, conexión a KMemory, lectura/escritura con MEMORY_DELAY |
| `feature/cpu/mmu` | MMU (traducción lógica→física), MOV_IN, MOV_OUT, COPY_MEM, MEM_ALLOC, MEM_FREE, comunicación con Memory Sticks |

> Crear `feature/memory-stick` desde `develop` al iniciar Fase 1. Crear `feature/cpu/mmu` desde `develop` al iniciar Fase 3.

---

## Fase 0 — Configuración del Entorno
**Fecha límite:** 13/04/2026

- [ ] Instalar `so-commons-library` y verificar compilación de `cpu` y `memory_stick`.
- [ ] Leer y entender la sección de CPU (MMU, instrucciones de memoria) y Memory Stick en la consigna.
- [ ] Coordinar con Nicolas el protocolo de mensajes CPU↔Memory Stick y KMemory↔Memory Stick.
- [ ] Coordinar con Kevin la división de tareas dentro del módulo CPU.

---

## Fase 1 — Check 1: Conexiones
**Fecha límite:** 18/04/2026
**Rama:** `feature/memory-stick`

- [ ] Implementar en **Memory Stick**:
  - Conexión cliente a **Kernel Memory** (registrarse con su tamaño).
  - Servidor de sockets para **CPUs** (múltiples CPUs pueden conectarse).
- [ ] Logear conexiones: `## Conectado a Kernel Memory`, `## CPU <ID CPU> Conectada`.

**Dependencias:** Coordinarse con Nicolas para usar el wrapper de sockets de Utils. Coordinarse con Luciano para el handshake Memory Stick↔Kernel Memory.

---

## Fase 2 — Check 2: Memory Stick Básico
**Fecha límite:** 23/05/2026
**Rama:** `feature/memory-stick`

### Semana 1–3 (19/04 – 09/05)
- [ ] Implementar en **Memory Stick** la inicialización del espacio de almacenamiento: asignar buffer en memoria RAM del tamaño recibido como argumento.
- [ ] Implementar la operación de **escritura**: recibir dirección física + datos → escribir en buffer → confirmar.
- [ ] Implementar la operación de **lectura**: recibir dirección física + tamaño → leer del buffer → retornar bytes.
- [ ] Aplicar el **MEMORY_DELAY** (espera configurable antes de responder cada operación).
- [ ] Implementar logs: `## Escritura de <N> bytes`, `## Lectura de <N> bytes`.

### Semana 4–5 (10/05 – 23/05)
- [ ] Manejar el acceso **concurrente** desde múltiples CPUs: proteger el buffer con mutex de pthread.
- [ ] Testear Memory Stick con un cliente de prueba (script o programa simple).

---

## Fase 3 — Check 3: CPU — MMU e Instrucciones de Memoria
**Fecha límite:** 20/06/2026
**Ramas:** `feature/cpu/mmu` (MMU e instrucciones) · `feature/memory-stick` (acceso concurrente)

### Semana 1 (24/05 – 30/05)
- [ ] Implementar la **MMU** en CPU:
  - Recibir tabla de segmentos desde Kernel Memory (parte del contexto).
  - Función de traducción: `num_segmento = dir_logica / SEGMENT_MAX_SIZE`, `desplazamiento = dir_logica % SEGMENT_MAX_SIZE`.
  - Identificar el Memory Stick de destino y la dirección física: `dir_fisica = base_segmento + desplazamiento`.
  - Detectar **Segmentation Fault**: si `desplazamiento + tamaño > límite_segmento` → finalizar proceso.

### Semana 2 (31/05 – 06/06)
- [ ] Implementar instrucción **MOV_IN \<Registro\>**: leer `tamaño(registro)` bytes de la dirección lógica en SI, solicitar lectura al Memory Stick correspondiente, almacenar en registro.
- [ ] Implementar instrucción **MOV_OUT \<Registro\>**: escribir el valor del registro en la dirección lógica de DI, solicitar escritura al Memory Stick correspondiente.
- [ ] Implementar instrucción **COPY_MEM \<Registro Tamaño\>**: copiar N bytes desde SI hasta DI (puede abarcar múltiples sticks).
- [ ] Implementar log: `PID: <PID> - Acción: <LEER/ESCRIBIR> - Dirección Física: <DIR> - Valor: <VALOR>`.

### Semana 3 (07/06 – 13/06)
- [ ] Implementar la **conexión de CPU a Memory Sticks**: al recibir un PID para ejecutar, CPU debe conectarse a los Memory Sticks necesarios (coordinar con Luciano cómo obtener la lista de sticks).
- [ ] Manejar operaciones de memoria que **abarcan múltiples Memory Sticks**: dividir la operación y consolidar respuesta.

### Semana 4 (14/06 – 20/06)
- [ ] Implementar syscall **MEM_ALLOC \<ID Segmento\> \<Tamaño\>**: enviar pedido a Kernel Scheduler, esperar confirmación, actualizar tabla de segmentos local.
- [ ] Implementar syscall **MEM_FREE \<ID Segmento\>**: enviar pedido a Kernel Scheduler, esperar confirmación, eliminar entrada de tabla de segmentos local.
- [ ] Testear el ciclo completo: MEM_ALLOC → MOV_OUT → MOV_IN → MEM_FREE.

---

## Fase 4 — Integración y Entrega Final
**Fecha límite:** 11/07/2026
**Rama:** `develop` (integración directa)

- [ ] Integrar y testear la MMU con Kernel Memory y Memory Sticks reales.
- [ ] Testear hot-plug de Memory Sticks durante ejecución.
- [ ] Testear COPY_MEM con datos que abarcan múltiples Memory Sticks.
- [ ] Verificar todos los logs obligatorios de CPU y Memory Stick.
- [ ] Colaborar en la resolución de bugs de integración.

---

## Interfaces a acordar con otros integrantes

| Con quién | Qué acordar |
|---|---|
| **Kevin** | División dentro de CPU: Kevin → ciclo básico + instrucciones aritméticas; Juan Manuel → MMU + instrucciones de memoria + syscalls MEM_ALLOC/MEM_FREE |
| **Luciano** | Protocolo CPU↔Memory Stick (lectura/escritura física), protocolo KMemory↔Memory Stick (mismo formato físico) |
| **Nicolas** | Formato de mensajes de syscalls MEM_ALLOC/MEM_FREE hacia Kernel Scheduler |
