#ifndef UTILS_PROTOCOLO_H_
#define UTILS_PROTOCOLO_H_

/*
 * protocolo.h — Códigos de operación del sistema
 *
 * Este enum define todos los tipos de mensaje que se intercambian entre módulos.
 * Cada vez que un módulo necesita un nuevo tipo de mensaje, tiene que agregarlo acá.
 *
 * REGLA: nunca cambiar el valor numérico de un código ya existente ni reordenar
 * los que ya están — eso rompería la comunicación con módulos que ya compilan.
 * Siempre agregar nuevos códigos ANTES de MSG_CANTIDAD.
 *
 * PARA EL CHECK 1 (conexión inicial):
 *   - Cada módulo que se conecta a otro tiene que identificarse al conectarse.
 *   - Agregar un MSG_<MODULO>_IDENTIFICACION por cada conexión nueva.
 *   Ejemplos de lo que van a necesitar:
 *     MSG_CPU_IDENTIFICACION        → CPU se conecta al Kernel Scheduler
 *     MSG_CPU_A_KERNEL_MEMORY       → CPU se conecta a Kernel Memory
 *     MSG_MEMORY_STICK_CONEXION     → Memory Stick se conecta a Kernel Memory
 *     MSG_SWAP_CONEXION             → Swap se conecta a Kernel Memory
 *
 * PARA EL CHECK 2 (funcionalidad básica):
 *   Agregar los mensajes propios de cada módulo. Ejemplos orientativos:
 *     MSG_DESPACHAR_PROCESO         → Kernel Scheduler → CPU: mandá a ejecutar este PID
 *     MSG_DEVOLVER_PROCESO          → CPU → Kernel Scheduler: terminé, acá el motivo
 *     MSG_FETCH_INSTRUCCION         → CPU → Kernel Memory: dame la instrucción en PC=X
 *     MSG_RESPUESTA_INSTRUCCION     → Kernel Memory → CPU: la instrucción es esta
 *     MSG_CREAR_PROCESO             → Kernel Scheduler → Kernel Memory: creá contexto para PID
 *     MSG_IO_PEDIDO                 → Kernel Scheduler → IO: hacé esta operación
 *     MSG_IO_FIN                    → IO → Kernel Scheduler: terminé la operación
 *
 * PARA EL CHECK 3 (memoria real, compactación, suspensión):
 *   Agregar los mensajes de memoria, compactación y suspensión. Ejemplos:
 *     MSG_LEER_MEMORIA              → CPU → Memory Stick: leé N bytes en dirección X
 *     MSG_ESCRIBIR_MEMORIA          → CPU → Memory Stick: escribí estos bytes en dirección X
 *     MSG_CREAR_SEGMENTO            → CPU → Kernel Memory (syscall MEM_ALLOC)
 *     MSG_ELIMINAR_SEGMENTO         → CPU → Kernel Memory (syscall MEM_FREE)
 *     MSG_COMPACTAR                 → Kernel Memory → Kernel Scheduler: necesito compactar
 *     MSG_FIN_COMPACTACION          → Kernel Scheduler → Kernel Memory: todas las CPUs desalojadas
 *     MSG_SUSPENDER_PROCESO         → Kernel Memory → Swap: guardá estos segmentos
 *     MSG_BSOD                      → Kernel Memory → Kernel Scheduler: Memory Stick caído
 */

typedef enum {
    // -----------------------------------------------------------------
    // IDENTIFICACIÓN — Check 1
    // -----------------------------------------------------------------
    MSG_IO_IDENTIFICACION,    // IO se presenta al Kernel Scheduler con su tipo

    MSG_CPU_IDENTIFICACION,   // CPU se presenta al Kernel Scheduler
    
    MSG_KS_IDENTIFICACION,    // Kernel Scheduler se presenta al Kernel Memory
    
    MSG_OK,                   // respuesta exitosa

    MSG_ERROR,

    MSG_MEMORY_STICK_IDENTIFICACION,  // Memory Stick → Kernel Memory
    MSG_SWAP_IDENTIFICACION,          // Swap → Kernel Memory

    // Marcador de fin — SIEMPRE tiene que ser el último
    MSG_CANTIDAD
} op_code;

#endif
