#ifndef UTILS_SOCKETS_H_
#define UTILS_SOCKETS_H_

/*
 * sockets.h — Funciones de red y serialización compartidas
 *
 * Incluir en cualquier módulo con:
 *   #include <utils/sockets.h>
 *   #include <utils/protocolo.h>
 *
 * Las funciones de red (crear_servidor, conectar_a_servidor, etc.) y las de
 * serialización (serializar_string, etc.) ya están implementadas y listas para usar.
 *
 * Si necesitan agregar funciones nuevas a utils, ver las secciones marcadas
 * con "ZONA DE EXTENSIÓN" al final de este archivo y en sockets.c.
 *
 * PARA EL CHECK 1:
 *   No deberían necesitar agregar nada acá. Las funciones existentes alcanzan
 *   para establecer conexiones e intercambiar el mensaje de identificación.
 *
 * PARA EL CHECK 2:
 *   Si necesitan serializar estructuras propias (contexto de CPU, segmentos, etc.)
 *   agregar las funciones de serialización en la zona indicada al final del archivo.
 *   Ejemplo de lo que van a necesitar:
 *     void*        serializar_contexto(t_contexto* ctx, uint32_t* out_size);
 *     t_contexto*  deserializar_contexto(void* payload);
 *     void*        serializar_instruccion(t_instruccion* inst, uint32_t* out_size);
 *     t_instruccion* deserializar_instruccion(void* payload);
 *
 * PARA EL CHECK 3:
 *   Agregar serialización de operaciones de memoria (lecturas/escrituras de bloques,
 *   tablas de segmentos, etc.).
 */

#include <stdint.h>
#include <stdlib.h>

// Estructura que representa un mensaje recibido
typedef struct {
    uint32_t op_code;
    uint32_t payload_size;
    void*    payload;
} t_mensaje;

/**
 * Crea un socket servidor que escucha en el puerto dado.
 * Retorna el file descriptor del servidor, o -1 si falla.
 */
int crear_servidor(int puerto);

/**
 * Bloquea hasta que un cliente se conecte al servidor.
 * Retorna el file descriptor del cliente conectado, o -1 si falla.
 */
int aceptar_conexion(int fd_servidor);

/**
 * Crea una conexión cliente al servidor en ip:puerto.
 * Retorna el file descriptor de la conexión, o -1 si falla.
 */
int conectar_a_servidor(char* ip, int puerto);

/**
 * Envía un mensaje por el socket fd.
 * Si payload es NULL, payload_size debe ser 0.
 */
void enviar_mensaje(int fd, uint32_t op_code, void* payload, uint32_t payload_size);

/**
 * Recibe un mensaje del socket fd.
 * Retorna un t_mensaje con el payload en heap (hay que liberarlo con free_mensaje).
 * Retorna NULL si la conexión se cerró.
 */
t_mensaje* recibir_mensaje(int fd);

/**
 * Libera la memoria de un mensaje recibido.
 */
void free_mensaje(t_mensaje* mensaje);

/**
 * Serializa un string en un buffer heap.
 * Retorna puntero al buffer y escribe el tamaño en 'out_size'.
 * El buffer incluye el '\0' final.
 * Hay que liberarlo con free().
 */
void* serializar_string(char* str, uint32_t* out_size);

/**
 * Deserializa un string desde el payload de un mensaje.
 * Retorna el string en heap. Hay que liberarlo con free().
 */
char* deserializar_string(void* payload);

/*
 * =====================================================================
 * ZONA DE EXTENSIÓN — Check 2 en adelante
 * =====================================================================
 */

// Registros de la CPU — usados en t_contexto
typedef struct {
    uint32_t pc;
    uint8_t  ax, bx, cx, dx;
    uint32_t eax, ebx, ecx, edx;
    uint32_t si, di;
} t_registros_cpu;

typedef struct {
    uint32_t id_segmento;
    uint32_t id_memory_stick;
    uint32_t base;
    uint32_t limite;
} t_entrada_segmento;

// Contexto de ejecución de un proceso — guardado/restaurado por Kernel Memory
typedef struct {
    uint32_t        pid;
    t_registros_cpu registros;
    uint32_t        cant_segmentos;
    t_entrada_segmento* segmentos;
} t_contexto;

// Pedido de fetch de instrucción — CPU → KM
typedef struct {
    uint32_t pid;
    uint32_t pc;
} t_fetch_request;

void*            serializar_contexto(t_contexto* ctx, uint32_t* out_size);
t_contexto*      deserializar_contexto(void* payload, uint32_t size);
t_fetch_request* deserializar_fetch_request(void* payload);
void             free_contexto(t_contexto* ctx);

#endif
