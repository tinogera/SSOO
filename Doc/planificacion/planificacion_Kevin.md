# Planificación Individual — Kevin Luciano Castillo Panta

## Módulos Asignados
- **Principal:** CPU — Ciclo de instrucción (fetch, decode, execute), instrucciones básicas
- **Secundario:** Kernel Memory — Gestión de instrucciones y contextos de proceso

## Ramas de trabajo

| Rama | Tareas |
|---|---|
| `feature/cpu/ciclo-basico` | Conexiones CPU, registros, fetch, decode, instrucciones básicas (NOOP/SET/SUM/SUB/JNZ), syscalls, interrupciones, INIT_PROC, EXIT |
| `feature/kernel-memory/conexiones-instrucciones` | Lectura de pseudocódigo, gestión de contextos por PID (en conjunto con Luciano) |

> Crear `feature/cpu/ciclo-basico` desde `develop` al iniciar Fase 1. Crear `feature/kernel-memory/conexiones-instrucciones` al inicio de Fase 2 (coordinar con Luciano quién hace el `git checkout -b`).

---

## Fase 0 — Configuración del Entorno
**Fecha límite:** 13/04/2026 — **COMPLETADA**

- [x] Instalar `so-commons-library` y verificar compilación del módulo `cpu`.
- [x] Leer y entender la sección de CPU en la consigna: registros, ciclo de instrucción, instrucciones a implementar.
- [x] Coordinar con Nicolas el diseño del protocolo de comunicación para los mensajes que CPU intercambia con Kernel Scheduler y Kernel Memory.

---

## Fase 1 — Check 1: Conexiones
**Fecha límite:** 18/04/2026 — **ENTREGADO (parcial)**
**Rama:** `feature/cpu/ciclo-basico`

- [x] Implementar conexión de CPU a **Kernel Scheduler** (recibir PID a ejecutar).
- [x] Implementar conexión de CPU a **Kernel Memory** (pedir instrucciones, enviar/recibir contexto).
- [x] Lograr que CPU se conecte, loguee la conexión y espere trabajos sin crashear.

**Dependencias:** Necesita que Utils (Nicolas) tenga el wrapper de sockets listo. Coordinarse para tener un mock de Kernel Scheduler y Kernel Memory para testear la conexión.

---

## Fase 2 — Check 2: Ciclo de Instrucción Básico
**Fecha límite:** 23/05/2026 — **COMPLETADA** (merge a main el 22/05/2026, commit `29f8611`)
**Ramas:** `feature/cpu/ciclo-basico` · `impactante`

### Semana 1–2 (19/04 – 02/05)
- [x] Definir e implementar la estructura de **registros de CPU**: PC (uint32_t), AX/BX/CX/DX (uint8_t), EAX/EBX/ECX/EDX (uint32_t), SI y DI (uint32_t).
- [x] Implementar el ciclo **Fetch**: solicitar instrucción a Kernel Memory usando el PC actual con `htonl`, recibir string de instrucción.
- [x] Implementar el ciclo **Decode**: parsear la instrucción recibida e identificar opcode y parámetros.

### Semana 3–4 (03/05 – 16/05)
- [x] Implementar instrucción **NOOP**.
- [x] Implementar instrucción **SET \<Registro\> \<Valor\>**.
- [x] Implementar instrucción **SUM \<Reg Destino\> \<Reg Origen\>**.
- [x] Implementar instrucción **SUB \<Reg Destino\> \<Reg Origen\>**.
- [x] Implementar instrucción **JNZ \<Registro\> \<Instrucción\>** (modificar PC si registro != 0).

### Semana 5 (17/05 – 23/05)
- [x] Implementar el envío de **syscalls** al Kernel Scheduler (MUTEX_CREATE, MUTEX_LOCK, MUTEX_UNLOCK, SLEEP, STDOUT, STDIN, EXIT) y la espera de respuesta.
- [x] Implementar recepción de **interrupciones** desde Kernel Scheduler (fin de quantum RR) con `select()` no bloqueante.
- [x] Al recibir interrupción: devolver proceso al Scheduler con motivo INTERRUPCION.
- [x] Implementar el **log obligatorio** de fetch: `## PID: <PID> - FETCH - Program Counter: <PC>`.
- [x] Implementar el **log obligatorio** de ejecución: `## PID: <PID> - Ejecutando: <INSTRUCCION> - <PARAMETROS>`.

---

## Fase 3 — Check 3: Kernel Memory (Contextos e Instrucciones) + INIT_PROC
**Fecha límite:** 20/06/2026
**Ramas:** `feature/kernel-memory/conexiones-instrucciones` · `feature/cpu/ciclo-basico`

> Lectura de pseudocódigo y gestión de contextos fueron adelantados a CK2.

### Semana 1–2 (24/05 – 06/06) — adelantado a CK2
- [x] Implementar en Kernel Memory la **lectura de archivos de pseudocódigo** (adelantado a CK2).
- [x] Aplicar el **INSTRUCTION_DELAY**.
- [x] Implementar **guardar/restaurar contexto** (CPU ↔ Kernel Memory) (adelantado a CK2).

### Semana 3–4 (07/06 – 20/06)
- [ ] Implementar en CPU la syscall **INIT_PROC** (crear proceso hijo): falta en `cpu_decode.c` y `cpu_ciclo.c`.
- [ ] Implementar en Kernel Memory log obligatorio: `## PID: <PID> - Obtener instrucción: <PC> - Instrucción: <INSTRUCCIÓN> <...ARGS>`.

---

## Fase 4 — Integración y Entrega Final
**Fecha límite:** 11/07/2026
**Rama:** `develop` (integración directa)

- [ ] Integrar y testear el ciclo completo de CPU con instrucciones básicas contra Kernel Memory real.
- [ ] Verificar que el log de interrupción esté implementado: `## Interrupción recibida`.
- [ ] Testear escenarios con múltiples CPUs corriendo en paralelo.
- [ ] Colaborar en la resolución de bugs de integración entre CPU y Kernel Memory.

---

## Interfaces a acordar con otros integrantes

| Con quién | Qué acordar |
|---|---|
| **Nicolas** | Formato de mensajes CPU↔KScheduler (envío de PID, syscalls, interrupciones, respuestas) |
| **Luciano** | Formato de mensajes CPU↔KMemory (fetch de instrucción, guardar/restaurar contexto) |
| **Juan Manuel** | División de responsabilidades CPU: Kevin maneja ciclo básico, Juan Manuel maneja MMU y memoria |
