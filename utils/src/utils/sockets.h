#ifndef UTILS_SOCKETS_H_
#define UTILS_SOCKETS_H_

#include <stdint.h>
#include <stdlib.h>

typedef struct {
    uint32_t op_code;
    uint32_t payload_size;
    void*    payload;
} t_mensaje;

int         crear_servidor(int puerto);
int         aceptar_conexion(int fd_servidor);
int         conectar_a_servidor(char* ip, int puerto);
void        enviar_mensaje(int fd, uint32_t op_code, void* payload, uint32_t payload_size);
t_mensaje*  recibir_mensaje(int fd);
void        free_mensaje(t_mensaje* mensaje);
void*       serializar_string(char* str, uint32_t* out_size);
char*       deserializar_string(void* payload);

// =====================================================================
// Estructuras Check 2
// =====================================================================

typedef struct {
    uint32_t pc;
    uint8_t  ax, bx, cx, dx;
    uint32_t eax, ebx, ecx, edx;
    uint32_t si, di;
} t_registros_cpu;

// Una entrada en la tabla de segmentos
typedef struct {
    uint32_t id_segmento;
    uint32_t id_memory_stick; // qué MS lo contiene
    uint32_t base;            // dirección física dentro del MS
    uint32_t limite;          // tamaño del segmento
} t_entrada_segmento;

// Contexto de ejecución completo
typedef struct {
    uint32_t          pid;
    t_registros_cpu   registros;
    uint32_t          cant_segmentos;
    t_entrada_segmento* segmentos;   // heap, cant_segmentos entradas
} t_contexto;

// Pedido de fetch — CPU → KM
typedef struct {
    uint32_t pid;
    uint32_t pc;
} t_fetch_request;

void*              serializar_contexto(t_contexto* ctx, uint32_t* out_size);
t_contexto*        deserializar_contexto(void* payload, uint32_t size);
t_fetch_request*   deserializar_fetch_request(void* payload);
void               free_contexto(t_contexto* ctx);

#endif
