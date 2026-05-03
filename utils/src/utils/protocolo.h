#ifndef UTILS_PROTOCOLO_H_
#define UTILS_PROTOCOLO_H_

/*
 * protocolo.h — Códigos de operación del sistema
 *
 * REGLA: nunca reordenar ni cambiar el valor de un código ya existente.
 * El enum asigna valores 0, 1, 2... en orden. Reordenar rompe la comunicación
 * con módulos que ya compilaron contra una versión anterior.
 * Siempre agregar nuevos códigos ANTES de MSG_CANTIDAD.
 *
 * PENDIENTE CHECK 3:
 *   MSG_DESPACHAR_PROCESO     → KS → CPU: mandar a ejecutar un PID
 *   MSG_DEVOLVER_PROCESO      → CPU → KS: fin de ejecución + motivo
 *   MSG_FETCH_INSTRUCCION     → CPU → KM: pedir instrucción en PC=X
 *   MSG_RESPUESTA_INSTRUCCION → KM → CPU: string de la instrucción
 *   MSG_CREAR_PROCESO         → KS → KM: inicializar contexto para un PID
 *   MSG_MEM_ALLOC             → CPU → KS (syscall MEM_ALLOC)
 *   MSG_MEM_FREE              → CPU → KS (syscall MEM_FREE)
 *   MSG_LEER_MEMORIA          → CPU → Memory Stick
 *   MSG_ESCRIBIR_MEMORIA      → CPU → Memory Stick
 *   MSG_COMPACTAR             → KM → KS: pedir compactación
 *   MSG_FIN_COMPACTACION      → KS → KM: CPUs desalojadas, podés compactar
 *   MSG_SUSPENDER_PROCESO     → KM → Swap: mover segmentos
 *   MSG_BSOD                  → KM → KS: Memory Stick caído
 */

typedef enum {
    // -----------------------------------------------------------------
    // IDENTIFICACIÓN — Check 1
    // -----------------------------------------------------------------
    MSG_IO_IDENTIFICACION,              // IO → KS:  payload: string tipo ("SLEEP"/"STDOUT"/"STDIN")
    MSG_CPU_IDENTIFICACION,             // CPU → KS: payload: string id_cpu
    MSG_KS_IDENTIFICACION,              // KS → KM:  sin payload
    MSG_OK,                             // respuesta exitosa — sin payload
    MSG_ERROR,                          // respuesta de error — sin payload
    MSG_MEMORY_STICK_IDENTIFICACION,    // MS → KM:  sin payload (tamaño pendiente CK2)
    MSG_SWAP_IDENTIFICACION,            // Swap → KM: sin payload (tamaño pendiente CK2)
    MSG_CPU_A_KERNEL_MEMORY,            // CPU → KM: sin payload

    // -----------------------------------------------------------------
    // IO — Check 2
    // -----------------------------------------------------------------
    MSG_IO_SLEEP,       // KS → IO SLEEP:  { uint32_t pid, uint32_t tiempo_ms }
    MSG_IO_STDOUT,      // KS → IO STDOUT: { uint32_t pid, char contenido[] }
    MSG_IO_STDIN,       // KS → IO STDIN:  { uint32_t pid, uint32_t n_bytes }
    MSG_IO_FIN,         // IO → KS:        { uint32_t pid }
    MSG_IO_STDIN_DATOS, // IO → KS:        { uint32_t pid, uint32_t n_bytes, uint8_t datos[] }

    // -----------------------------------------------------------------
    // MUTEX — Check 2
    // -----------------------------------------------------------------
    MSG_MUTEX_CREATE,   // CPU → KS: { uint32_t pid, char nombre[] }
    MSG_MUTEX_LOCK,     // CPU → KS: { uint32_t pid, char nombre[] }
    MSG_MUTEX_UNLOCK,   // CPU → KS: { uint32_t pid, char nombre[] }

    // Marcador de fin — SIEMPRE tiene que ser el último
    MSG_CANTIDAD
} op_code;

// ---------------------------------------------------------------------------
// Structs de payload — Check 2
// __attribute__((packed)) elimina el padding del compilador para que el layout
// en memoria sea exactamente el que se envía por el socket.
// ---------------------------------------------------------------------------
typedef struct __attribute__((packed)) {
    uint32_t pid;
    uint32_t tiempo_ms;
} t_payload_io_sleep;

typedef struct __attribute__((packed)) {
    uint32_t pid;
    uint32_t n_bytes;
} t_payload_io_stdin;

typedef struct __attribute__((packed)) {
    uint32_t pid;
} t_payload_io_fin;

typedef struct __attribute__((packed)) {
    uint32_t pid;
    char     nombre[]; // flexible array — el nombre del mutex sigue inmediatamente
} t_payload_mutex;

#endif
