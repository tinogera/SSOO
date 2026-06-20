#ifndef UTILS_PROTOCOLO_H_
#define UTILS_PROTOCOLO_H_

#include <stdint.h>

typedef enum {
    // -----------------------------------------------------------------
    // IDENTIFICACIÓN — Check 1
    // -----------------------------------------------------------------
    MSG_IO_IDENTIFICACION,              // 0
    MSG_CPU_IDENTIFICACION,             // 1
    MSG_KS_IDENTIFICACION,              // 2
    MSG_OK,                             // 3
    MSG_ERROR,                          // 4
    MSG_MEMORY_STICK_IDENTIFICACION,    // 5
    MSG_SWAP_IDENTIFICACION,            // 6
    MSG_CPU_A_KERNEL_MEMORY,            // 7

    // -----------------------------------------------------------------
    // IO — Check 2
    // -----------------------------------------------------------------
    MSG_IO_SLEEP,       // 8
    MSG_IO_STDOUT,      // 9
    MSG_IO_STDIN,       // 10
    MSG_IO_FIN,         // 11
    MSG_IO_STDIN_DATOS, // 12

    // -----------------------------------------------------------------
    // MUTEX — Check 2
    // -----------------------------------------------------------------
    MSG_MUTEX_CREATE,   // 13
    MSG_MUTEX_LOCK,     // 14
    MSG_MUTEX_UNLOCK,   // 15

    // -----------------------------------------------------------------
    // CPU ↔ KS — Check 2
    // -----------------------------------------------------------------
    MSG_INIT_PROC,          // 16  CPU → KS: syscall INIT_PROC { uint32_t pid, uint32_t prioridad, char archivo[] }
    MSG_DESPACHAR_PROCESO,  // 17  KS → CPU: { uint32_t pid }
    MSG_DEVOLVER_PROCESO,   // 18  CPU → KS: { uint32_t pid, uint32_t motivo, uint32_t pc }
    MSG_INTERRUPCION_CPU,   // 19  KS → CPU: { uint32_t pid, uint32_t motivo }

    // -----------------------------------------------------------------
    // CPU ↔ KM — Check 2
    // -----------------------------------------------------------------
    MSG_FETCH_INSTRUCCION,      // 20
    MSG_RESPUESTA_INSTRUCCION,  // 21
    MSG_CREAR_PROCESO,          // 22
    MSG_GUARDAR_CONTEXTO,       // 23
    MSG_RESTAURAR_CONTEXTO,     // 24

    // -----------------------------------------------------------------
    // Syscalls CPU → KS — Check 2
    // -----------------------------------------------------------------
    MSG_SYSCALL_SLEEP,   // 25  CPU → KS: { uint32_t pid, uint32_t tiempo_ms }
    MSG_SYSCALL_STDOUT,  // 26  CPU → KS: { uint32_t pid, uint32_t direccion_logica, uint32_t tamanio }
    MSG_SYSCALL_STDIN,   // 27  CPU → KS: { uint32_t pid, uint32_t direccion_logica, uint32_t tamanio }
    MSG_SYSCALL_EXIT,    // 28  CPU → KS: { uint32_t pid }

    // -----------------------------------------------------------------
    // MS ↔ CPU — Check 2
    // -----------------------------------------------------------------
    MSG_MEMORY_WRITE,           // 29
    MSG_MEMORY_READ,            // 30
    MSG_MEMORY_READ_RESPUESTA,  // 31

    // -----------------------------------------------------------------
    // Check 3 — Memoria, segmentos, compactación, suspensión
    // -----------------------------------------------------------------
    MSG_MEM_ALLOC,              // 32  CPU → KS: syscall MEM_ALLOC { pid, id_seg, tamanio }
    MSG_MEM_FREE,               // 33  CPU → KS: syscall MEM_FREE  { pid, id_seg }
    MSG_CREAR_SEGMENTO,         // 34  KS → KM:  { pid, id_seg, tamanio }
    MSG_ELIMINAR_SEGMENTO,      // 35  KS → KM:  { pid, id_seg }
    MSG_LEER_MEMORIA,           // 36  KM → MS:  { dir_fisica, tamanio }
    MSG_LEER_MEMORIA_RESP,      // 37  MS → KM:  { bytes[] }
    MSG_ESCRIBIR_MEMORIA,       // 38  KM → MS:  { dir_fisica, tamanio, bytes[] }
    MSG_LEER_DATOS,             // 39  KS → KM:  { pid, dir_logica, tamanio }
    MSG_LEER_DATOS_RESP,        // 40  KM → KS:  { bytes[] }
    MSG_ESCRIBIR_DATOS,         // 41  KS → KM:  { pid, dir_logica, tamanio, bytes[] }
    MSG_COMPACTAR,              // 42  KM → KS:  sin payload — pedir desalojo CPUs
    MSG_FIN_COMPACTACION,       // 43  KS → KM:  sin payload — CPUs desalojadas
    MSG_SUSPENDER_PROCESO,      // 44  KS → KM:  { uint32_t pid }
    MSG_DESSUSPENDER_PROCESO,   // 45  KS → KM:  { uint32_t pid }
    MSG_MAS_MEMORIA,            // 46  KM → KS:  sin payload — nuevo MS conectado
    MSG_BSOD,                   // 47  KM → KS:  sin payload — MS caído
    MSG_SWAP_LEER,              // 48  KM → Swap: { uint32_t nro_bloque }
    MSG_SWAP_LEER_RESP,         // 49  Swap → KM: { bytes[] }
    MSG_SWAP_ESCRIBIR,          // 50  KM → Swap: { uint32_t nro_bloque, bytes[] }
    MSG_TABLA_SEGMENTOS,        // 51  KM → CPU (en contexto restaurado): segmentos actualizados

    MSG_CANTIDAD
} op_code;

// ---------------------------------------------------------------------------
// Structs de payload (packed — sin padding)
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
    char     nombre[];
} t_payload_mutex;

typedef struct __attribute__((packed)) {
    uint32_t pid;
    uint32_t pc;
} t_payload_fetch_instruccion;

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

typedef struct __attribute__((packed)) {
    uint32_t pid;
    uint32_t prioridad;
    char archivo[];
} t_payload_syscall_init_proc;

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

typedef struct __attribute__((packed)) {
    uint32_t pid;
    uint32_t id_segmento;
    uint32_t tamanio;
} t_payload_crear_segmento;
typedef t_payload_crear_segmento t_payload_syscall_mem_alloc;

typedef struct __attribute__((packed)) {
    uint32_t pid;
    uint32_t id_segmento;
} t_payload_eliminar_segmento;
typedef t_payload_eliminar_segmento t_payload_syscall_mem_free;

typedef struct __attribute__((packed)) {
    uint32_t dir_fisica;
    uint32_t tamanio;
} t_payload_leer_memoria;

typedef struct __attribute__((packed)) {
    uint32_t dir_fisica;
    uint32_t tamanio;
    // bytes siguen inmediatamente
} t_payload_escribir_memoria;

typedef struct __attribute__((packed)) {
    uint32_t pid;
    uint32_t dir_logica;
    uint32_t tamanio;
} t_payload_acceso_datos;

typedef struct __attribute__((packed)) {
    uint32_t nro_bloque;
} t_payload_swap_leer;

typedef struct __attribute__((packed)) {
    uint32_t nro_bloque;
    // bytes del bloque siguen inmediatamente
} t_payload_swap_escribir;


typedef enum {
    MOTIVO_INTERRUPCION_QUANTUM  = 0,
    MOTIVO_INTERRUPCION_DESALOJO = 1
} t_motivo_interrupcion_cpu;

typedef enum {
    MOTIVO_DEVOLUCION_SYSCALL      = 0,
    MOTIVO_DEVOLUCION_EXIT         = 1,
    MOTIVO_DEVOLUCION_ERROR        = 2,
    MOTIVO_DEVOLUCION_INTERRUPCION = 3,
    MOTIVO_DEVOLUCION_SEG_FAULT    = 4
} t_motivo_devolucion_cpu;

#endif
