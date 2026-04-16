#include <utils/sockets.h>

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

// ---------------------------------------------------------------------------
// Función interna: enviar exactamente N bytes por el socket
// ---------------------------------------------------------------------------
static int enviar_bytes(int fd, void* buffer, size_t size) {
    size_t enviado = 0;
    while (enviado < size) {
        ssize_t resultado = send(fd, buffer + enviado, size - enviado, MSG_NOSIGNAL);
        if (resultado <= 0) return -1;
        enviado += resultado;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Función interna: recibir exactamente N bytes del socket
// ---------------------------------------------------------------------------
static int recibir_bytes(int fd, void* buffer, size_t size) {
    size_t recibido = 0;
    while (recibido < size) {
        ssize_t resultado = recv(fd, buffer + recibido, size - recibido, 0);
        if (resultado <= 0) return -1;
        recibido += resultado;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// crear_servidor
// ---------------------------------------------------------------------------
int crear_servidor(int puerto) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        return -1;
    }

    // Permite reutilizar el puerto inmediatamente después de cerrar el proceso
    int reuseaddr = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuseaddr, sizeof(reuseaddr));

    struct sockaddr_in addr = {
        .sin_family      = AF_INET,
        .sin_addr.s_addr = INADDR_ANY,
        .sin_port        = htons(puerto),
    };

    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(fd);
        return -1;
    }

    if (listen(fd, 10) < 0) {
        perror("listen");
        close(fd);
        return -1;
    }

    return fd;
}

// ---------------------------------------------------------------------------
// aceptar_conexion
// ---------------------------------------------------------------------------
int aceptar_conexion(int fd_servidor) {
    struct sockaddr_in cliente_addr;
    socklen_t addr_len = sizeof(cliente_addr);

    int fd_cliente = accept(fd_servidor, (struct sockaddr*)&cliente_addr, &addr_len);
    if (fd_cliente < 0) {
        perror("accept");
        return -1;
    }

    return fd_cliente;
}

// ---------------------------------------------------------------------------
// conectar_a_servidor
// ---------------------------------------------------------------------------
int conectar_a_servidor(char* ip, int puerto) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        return -1;
    }

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port   = htons(puerto),
    };

    if (inet_pton(AF_INET, ip, &addr.sin_addr) <= 0) {
        fprintf(stderr, "IP inválida: %s\n", ip);
        close(fd);
        return -1;
    }

    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("connect");
        close(fd);
        return -1;
    }

    return fd;
}

// ---------------------------------------------------------------------------
// enviar_mensaje
// ---------------------------------------------------------------------------
void enviar_mensaje(int fd, uint32_t op_code, void* payload, uint32_t payload_size) {
    uint32_t code_n = htonl(op_code);
    enviar_bytes(fd, &code_n, sizeof(code_n));

    uint32_t size_n = htonl(payload_size);
    enviar_bytes(fd, &size_n, sizeof(size_n));

    if (payload_size > 0 && payload != NULL) {
        enviar_bytes(fd, payload, payload_size);
    }
}

// ---------------------------------------------------------------------------
// recibir_mensaje
// ---------------------------------------------------------------------------
t_mensaje* recibir_mensaje(int fd) {
    uint32_t op_code_n, size_n;

    if (recibir_bytes(fd, &op_code_n, sizeof(op_code_n)) < 0) return NULL;
    if (recibir_bytes(fd, &size_n, sizeof(size_n)) < 0) return NULL;

    t_mensaje* msg = malloc(sizeof(t_mensaje));
    msg->op_code      = ntohl(op_code_n);
    msg->payload_size = ntohl(size_n);
    msg->payload      = NULL;

    if (msg->payload_size > 0) {
        msg->payload = malloc(msg->payload_size);
        if (recibir_bytes(fd, msg->payload, msg->payload_size) < 0) {
            free(msg->payload);
            free(msg);
            return NULL;
        }
    }

    return msg;
}

// ---------------------------------------------------------------------------
// free_mensaje
// ---------------------------------------------------------------------------
void free_mensaje(t_mensaje* mensaje) {
    if (mensaje == NULL) return;
    if (mensaje->payload != NULL) free(mensaje->payload);
    free(mensaje);
}

// ---------------------------------------------------------------------------
// serializar_string
// ---------------------------------------------------------------------------
void* serializar_string(char* str, uint32_t* out_size) {
    *out_size = strlen(str) + 1;
    void* buffer = malloc(*out_size);
    memcpy(buffer, str, *out_size);
    return buffer;
}

// ---------------------------------------------------------------------------
// deserializar_string
// ---------------------------------------------------------------------------
char* deserializar_string(void* payload) {
    return strdup((char*)payload);
}

// =============================================================================
// ZONA DE EXTENSIÓN — Check 2 en adelante
// =============================================================================
// Agregar acá la implementación de las funciones de serialización para
// estructuras propias de cada módulo (declaradas en sockets.h).
//
// Patrón de serialización recomendado para estructuras con campos de tamaño fijo:
//
//   void* serializar_contexto(t_contexto* ctx, uint32_t* out_size) {
//       *out_size = sizeof(t_contexto);
//       void* buf = malloc(*out_size);
//       memcpy(buf, ctx, *out_size);
//       return buf;
//   }
//
//   t_contexto* deserializar_contexto(void* payload) {
//       t_contexto* ctx = malloc(sizeof(t_contexto));
//       memcpy(ctx, payload, sizeof(t_contexto));
//       return ctx;
//   }
//
// Para estructuras con strings o listas (tamaño variable), se serializa campo
// por campo: primero el tamaño de cada campo (uint32_t en network byte order),
// luego los bytes del campo. Consultar con Nicolas si tienen dudas.
// =============================================================================
