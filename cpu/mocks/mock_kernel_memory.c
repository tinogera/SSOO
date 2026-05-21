#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <utils/protocolo.h>
#include <utils/sockets.h>

static const char* instruccion_por_pc(uint32_t pc) {
    static const char* instrucciones[] = {
        "SET AX 2",
        "SET BX 3",
        "SUM AX BX",
        "EXIT"
    };

    uint32_t cantidad = sizeof(instrucciones) / sizeof(instrucciones[0]);
    if (pc >= cantidad) {
        return "EXIT";
    }

    return instrucciones[pc];
}

static void responder_fetch(int fd_cpu, t_mensaje* mensaje) {
    if (mensaje->payload_size != sizeof(t_payload_fetch_instruccion)) {
        fprintf(stderr, "Payload FETCH invalido\n");
        enviar_mensaje(fd_cpu, MSG_ERROR, NULL, 0);
        return;
    }

    t_payload_fetch_instruccion* pedido = (t_payload_fetch_instruccion*) mensaje->payload;
    const char* instruccion = instruccion_por_pc(pedido->pc);

    printf("FETCH recibido - PID=%u PC=%u -> %s\n", pedido->pid, pedido->pc, instruccion);

    uint32_t payload_size;
    void* payload = serializar_string((char*) instruccion, &payload_size);
    enviar_mensaje(fd_cpu, MSG_RESPUESTA_INSTRUCCION, payload, payload_size);
    free(payload);
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Uso: %s [PUERTO]\n", argv[0]);
        return EXIT_FAILURE;
    }

    int puerto = atoi(argv[1]);
    int fd_servidor = crear_servidor(puerto);
    if (fd_servidor < 0) {
        fprintf(stderr, "No se pudo levantar Mock Kernel Memory en puerto %d\n", puerto);
        return EXIT_FAILURE;
    }

    printf("Mock Kernel Memory escuchando en puerto %d\n", puerto);

    int fd_cpu = aceptar_conexion(fd_servidor);
    if (fd_cpu < 0) {
        fprintf(stderr, "No se pudo aceptar conexion de CPU\n");
        close(fd_servidor);
        return EXIT_FAILURE;
    }

    t_mensaje* handshake = recibir_mensaje(fd_cpu);
    if (handshake == NULL || handshake->op_code != MSG_CPU_A_KERNEL_MEMORY) {
        fprintf(stderr, "Handshake invalido de CPU\n");
        if (handshake != NULL) {
            free_mensaje(handshake);
        }
        close(fd_cpu);
        close(fd_servidor);
        return EXIT_FAILURE;
    }

    free_mensaje(handshake);
    enviar_mensaje(fd_cpu, MSG_OK, NULL, 0);
    printf("CPU conectada al Mock Kernel Memory\n");

    while (1) {
        t_mensaje* mensaje = recibir_mensaje(fd_cpu);
        if (mensaje == NULL) {
            printf("CPU desconectada\n");
            break;
        }

        if (mensaje->op_code == MSG_FETCH_INSTRUCCION) {
            responder_fetch(fd_cpu, mensaje);
        } else {
            fprintf(stderr, "Mensaje inesperado en Mock Kernel Memory: op_code=%u\n", mensaje->op_code);
            enviar_mensaje(fd_cpu, MSG_ERROR, NULL, 0);
        }

        free_mensaje(mensaje);
    }

    close(fd_cpu);
    close(fd_servidor);
    return EXIT_SUCCESS;
}
