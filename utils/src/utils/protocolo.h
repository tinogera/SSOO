#ifndef UTILS_PROTOCOLO_H_
#define UTILS_PROTOCOLO_H_

#include <stdint.h>

/*
 * protocolo.h — Códigos de operación del sistema
 *
 * REGLA: nunca reordenar ni cambiar el valor de un código ya existente.
 * El enum asigna valores 0, 1, 2... en orden. Reordenar rompe la comunicación
 * con módulos que ya compilaron contra una versión anterior.
 * Siempre agregar nuevos códigos ANTES de MSG_CANTIDAD.
 *
 * PENDIENTE CHECK 3:
 *   MSG_MEM_ALLOC          → CPU → KS (syscall MEM_ALLOC)
 *   MSG_MEM_FREE           → CPU → KS (syscall MEM_FREE)
 *   MSG_LEER_MEMORIA       → CPU → Memory Stick
 *   MSG_ESCRIBIR_MEMORIA   → CPU → Memory Stick
 *   MSG_COMPACTAR          → KM → KS: pedir compactación
 *   MSG_FIN_COMPACTACION   → KS → KM: CPUs desalojadas, podés compactar
 *   MSG_SUSPENDER_PROCESO  → KM → Swap: mover segmentos
 *   MSG_BSOD               → KM → KS: Memory Stick caído
 */

typedef enum {
    // -----------------------------------------------------------------
    // IDENTIFICACIÓN — Check 1 (valores 0–7, no modificar)
    // -----------------------------------------------------------------
    MSG_IO_IDENTIFICACION,              // 0  IO → KS:  payload: string tipo ("SLEEP"/"STDOUT"/"STDIN")
    MSG_CPU_IDENTIFICACION,             // 1  CPU → KS: payload: string id_cpu
    MSG_KS_IDENTIFICACION,              // 2  KS → KM:  sin payload
    MSG_OK,                             // 3  respuesta exitosa — sin payload
    MSG_ERROR,                          // 4  respuesta de error — sin payload
    MSG_MEMORY_STICK_IDENTIFICACION,    // 5  MS → KM:  payload: uint32_t tamanio
    MSG_SWAP_IDENTIFICACION,            // 6  Swap → KM: payload: uint32_t swap_size, uint32_t block_size
    MSG_CPU_A_KERNEL_MEMORY,            // 7  CPU → KM: sin payload

    // -----------------------------------------------------------------
    // IO — Check 2 (8–12)
    // -----------------------------------------------------------------
    MSG_IO_SLEEP,       // 8   KS → IO SLEEP:  { uint32_t pid, uint32_t tiempo_ms }
    MSG_IO_STDOUT,      // 9   KS → IO STDOUT: { uint32_t pid, char contenido[] }
    MSG_IO_STDIN,       // 10  KS → IO STDIN:  { uint32_t pid, uint32_t n_bytes }
    MSG_IO_FIN,         // 11  IO → KS:        { uint32_t pid }
    MSG_IO_STDIN_DATOS, // 12  IO → KS:        { uint32_t pid, uint32_t n_bytes, uint8_t datos[] }

    // -----------------------------------------------------------------
    // MUTEX — Check 2 (13–15)
    // -----------------------------------------------------------------
    MSG_MUTEX_CREATE,   // 13  CPU → KS: { uint32_t pid, char nombre[] }
    MSG_MUTEX_LOCK,     // 14  CPU → KS: { uint32_t pid, char nombre[] }
    MSG_MUTEX_UNLOCK,   // 15  CPU → KS: { uint32_t pid, char nombre[] }

    // -----------------------------------------------------------------
    // CPU ↔ KS — Check 2 (16–19)
    // -----------------------------------------------------------------
    MSG_INIT_PROC,          // 16  CPU → KS: syscall INIT_PROC { uint32_t pid, char archivo[], uint32_t prioridad }
    MSG_DESPACHAR_PROCESO,  // 17  KS → CPU: { uint32_t pid }
    MSG_DEVOLVER_PROCESO,   // 18  CPU → KS: { uint32_t pid, uint32_t motivo, uint32_t pc }
    MSG_INTERRUPCION_CPU,   // 19  KS → CPU: { uint32_t pid, uint32_t motivo }

    // -----------------------------------------------------------------
    // CPU ↔ KM — Check 2 (20–24)
    // -----------------------------------------------------------------
    MSG_FETCH_INSTRUCCION,      // 20  CPU → KM: { uint32_t pid, uint32_t pc }
    MSG_RESPUESTA_INSTRUCCION,  // 21  KM → CPU: payload: string instrucción
    MSG_CREAR_PROCESO,          // 22  KS → KM:  { uint32_t pid, char path[] }
    MSG_GUARDAR_CONTEXTO,       // 23  CPU → KM: t_contexto serializado
    MSG_RESTAURAR_CONTEXTO,     // 24  KM → CPU: t_contexto serializado (respuesta a pedido por pid)

    // -----------------------------------------------------------------
    // Syscalls CPU → KS — Check 2 (25–28)
    // -----------------------------------------------------------------
    MSG_SYSCALL_SLEEP,   // 25  CPU → KS: { uint32_t pid, uint32_t tiempo_ms }
    MSG_SYSCALL_STDOUT,  // 26  CPU → KS: { uint32_t pid, uint32_t direccion_logica, uint32_t tamanio }
    MSG_SYSCALL_STDIN,   // 27  CPU → KS: { uint32_t pid, uint32_t direccion_logica, uint32_t tamanio }
    MSG_SYSCALL_EXIT,    // 28  CPU → KS: { uint32_t pid }

    // -----------------------------------------------------------------
    // MS ↔ CPU — Check 2 (28–31)
    // -----------------------------------------------------------------
    MSG_MEMORY_WRITE,
    MSG_MEMORY_READ,
    MSG_MEMORY_READ_RESPUESTA,

    // Marcador de fin — SIEMPRE tiene que ser el último
    MSG_CANTIDAD
} op_code;

// ---------------------------------------------------------------------------
// Structs de payload
// __attribute__((packed)) elimina el padding del compilador para que el layout
// en memoria sea exactamente el que se envía por el socket.
// ---------------------------------------------------------------------------

// --- IO ---
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

// --- Mutex ---
typedef struct __attribute__((packed)) {
    uint32_t pid;
    char     nombre[]; // flexible array — el nombre del mutex sigue inmediatamente
} t_payload_mutex;

// --- CPU ↔ KM ---
typedef struct __attribute__((packed)) {
    uint32_t pid;
    uint32_t pc;
} t_payload_fetch_instruccion;

// --- CPU ↔ KS: despacho y devolución ---
typedef struct __attribute__((packed)) {
    uint32_t pid;
} t_payload_despachar_proceso;

typedef struct __attribute__((packed)) {
    uint32_t pid;
    uint32_t motivo;
    uint32_t pc;
} t_payload_devolver_proceso;

typedef struct __attribute__((packed)) {
    uint32_t pid;
    uint32_t motivo;
} t_payload_interrupcion_cpu;

// --- Syscalls CPU → KS ---
typedef struct __attribute__((packed)) {
    uint32_t pid;
    uint32_t tiempo_ms;
} t_payload_syscall_sleep;

typedef struct __attribute__((packed)) {
    uint32_t pid;
    uint32_t direccion_logica;
    uint32_t tamanio;
} t_payload_syscall_io_memoria;

typedef struct __attribute__((packed)) {
    uint32_t pid;
} t_payload_syscall_exit;

// --- Enums de motivos ---
typedef enum {
    MOTIVO_INTERRUPCION_QUANTUM  = 0,
    MOTIVO_INTERRUPCION_DESALOJO = 1
} t_motivo_interrupcion_cpu;

typedef enum {
    MOTIVO_DEVOLUCION_SYSCALL      = 0,
    MOTIVO_DEVOLUCION_EXIT         = 1,
    MOTIVO_DEVOLUCION_ERROR        = 2,
    MOTIVO_DEVOLUCION_INTERRUPCION = 3
} t_motivo_devolucion_cpu;

#endif
