# Planificación Individual — Kevin Luciano Castillo Panta

## Módulos Asignados
- **Principal:** CPU — Ciclo de instrucción (fetch, decode, execute), instrucciones básicas
- **Secundario:** Kernel Memory — Gestión de instrucciones y contextos de proceso

---

## Fase 0 — Configuración del Entorno
**Fecha límite:** 13/04/2026

- [ ] Instalar `so-commons-library` y verificar compilación del módulo `cpu`.
- [ ] Leer y entender la sección de CPU en la consigna: registros, ciclo de instrucción, instrucciones a implementar.
- [ ] Coordinar con Nicolas el diseño del protocolo de comunicación para los mensajes que CPU intercambia con Kernel Scheduler y Kernel Memory.

---

## Fase 1 — Check 1: Conexiones
**Fecha límite:** 18/04/2026

- [ ] Implementar conexión de CPU a **Kernel Scheduler** (recibir PID a ejecutar).
- [ ] Implementar conexión de CPU a **Kernel Memory** (pedir instrucciones, enviar/recibir contexto).
- [ ] Lograr que CPU se conecte, loguee la conexión y espere trabajos sin crashear.

**Dependencias:** Necesita que Utils (Nicolas) tenga el wrapper de sockets listo. Coordinarse para tener un mock de Kernel Scheduler y Kernel Memory para testear la conexión.

---

## Fase 2 — Check 2: Ciclo de Instrucción Básico
**Fecha límite:** 23/05/2026

### Semana 1–2 (19/04 – 02/05)
- [ ] Definir e implementar la estructura de **registros de CPU**: PC (uint32_t), AX/BX/CX/DX (uint8_t), EAX/EBX/ECX/EDX (uint32_t), SI y DI (uint32_t).
- [ ] Implementar el ciclo **Fetch**: solicitar instrucción a Kernel Memory usando el PC actual, recibir string de instrucción.
- [ ] Implementar el ciclo **Decode**: parsear la instrucción recibida e identificar opcode y parámetros.

### Semana 3–4 (03/05 – 16/05)
- [ ] Implementar instrucción **NOOP**.
- [ ] Implementar instrucción **SET \<Registro\> \<Valor\>**.
- [ ] Implementar instrucción **SUM \<Reg Destino\> \<Reg Origen\>**.
- [ ] Implementar instrucción **SUB \<Reg Destino\> \<Reg Origen\>**.
- [ ] Implementar instrucción **JNZ \<Registro\> \<Instrucción\>** (modificar PC si registro != 0).

### Semana 5 (17/05 – 23/05)
- [ ] Implementar el envío de **syscalls** al Kernel Scheduler (MUTEX_CREATE, MUTEX_LOCK, MUTEX_UNLOCK, SLEEP, STDOUT, STDIN, EXIT) y la espera de respuesta.
- [ ] Implementar recepción de **interrupciones** desde Kernel Scheduler (fin de quantum RR, desalojo por CMN).
- [ ] Al recibir interrupción: guardar contexto en Kernel Memory y notificar al Scheduler.
- [ ] Implementar el **log obligatorio** de fetch: `## PID: <PID> - FETCH - Program Counter: <PC>`.
- [ ] Implementar el **log obligatorio** de ejecución: `## PID: <PID> - Ejecutando: <INSTRUCCION> - <PARAMETROS>`.

---

## Fase 3 — Check 3: Kernel Memory (Contextos e Instrucciones)
**Fecha límite:** 20/06/2026

> En esta fase Kevin contribuye al módulo Kernel Memory para la gestión de contextos y la lectura de instrucciones.

### Semana 1–2 (24/05 – 06/06)
- [ ] Implementar en Kernel Memory la **lectura de archivos de pseudocódigo**: dado un path y un número de línea (PC), retornar la instrucción correspondiente.
- [ ] Aplicar el **INSTRUCTION_DELAY** (delay configurable antes de responder la instrucción).
- [ ] Implementar el **log obligatorio**: `## PID: <PID> - Obtener instrucción: <PC> - Instrucción: <INSTRUCCIÓN> <...ARGS>`.

### Semana 3–4 (07/06 – 20/06)
- [ ] Implementar en Kernel Memory la **creación de contexto de proceso**: al recibir INIT_PROC del Scheduler, inicializar todos los registros a 0 y asociarlos al PID.
- [ ] Implementar **guardar contexto** (CPU → Kernel Memory): recibir todos los registros + tabla de segmentos y almacenarlos por PID.
- [ ] Implementar **restaurar contexto** (Kernel Memory → CPU): enviar todos los registros + tabla de segmentos para el PID solicitado.
- [ ] Implementar en CPU las syscalls **INIT_PROC** y **EXIT** (crear proceso hijo y finalizar).

---

## Fase 4 — Integración y Entrega Final
**Fecha límite:** 11/07/2026

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
