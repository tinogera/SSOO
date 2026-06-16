#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <commons/log.h>
#include <commons/config.h>
#include <utils/sockets.h>
#include <utils/protocolo.h>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Uso: %s [CONFIG]\n", argv[0]);
        return EXIT_FAILURE;
    }

    t_config* config = config_create(argv[1]);
    if (!config) { fprintf(stderr, "Error leyendo config\n"); return EXIT_FAILURE; }

    t_log* logger = log_create("swap.log", "Swap", true, LOG_LEVEL_INFO);
    if (!logger) { fprintf(stderr, "Error creando logger\n"); return EXIT_FAILURE; }

    char*    ip_km      = config_get_string_value(config, "KERNEL_MEMORY_IP");
    int      puerto_km  = config_get_int_value(config,    "KERNEL_MEMORY_PORT");
    uint32_t swap_size  = (uint32_t)config_get_int_value(config, "SWAP_FILE_SIZE");
    uint32_t block_size = (uint32_t)config_get_int_value(config, "BLOCK_SIZE");
    char*    swap_path  = config_get_string_value(config, "SWAP_FILE_PATH");

    // Abrir archivo de swap
    FILE* swap_file = fopen(swap_path, "w+b");
    if (!swap_file) {
        log_error(logger, "No se pudo abrir el archivo de swap: %s", swap_path);
        return EXIT_FAILURE;
    }
    // Reservar espacio
    fseek(swap_file, swap_size - 1, SEEK_SET);
    fputc(0, swap_file);
    fflush(swap_file);

    int fd = conectar_a_servidor(ip_km, puerto_km);
    if (fd < 0) {
        log_error(logger, "No se pudo conectar a Kernel Memory en %s:%d", ip_km, puerto_km);
        return EXIT_FAILURE;
    }

    // Identificarse con tamaño y bloque
    uint8_t payload[8];
    uint32_t ss_n = htonl(swap_size);
    uint32_t bs_n = htonl(block_size);
    memcpy(payload,     &ss_n, 4);
    memcpy(payload + 4, &bs_n, 4);
    enviar_mensaje(fd, MSG_SWAP_IDENTIFICACION, payload, sizeof(payload));

    t_mensaje* resp = recibir_mensaje(fd);
    if (!resp || resp->op_code != MSG_OK) {
        log_error(logger, "Kernel Memory rechazó la conexión");
        if (resp) free_mensaje(resp);
        return EXIT_FAILURE;
    }
    free_mensaje(resp);
    log_info(logger, "## Conectado a Kernel Memory");

    // Loop de atención
    t_mensaje* pedido;
    while ((pedido = recibir_mensaje(fd)) != NULL) {

        if (pedido->op_code == MSG_SWAP_LEER) {
            uint32_t nb_n; memcpy(&nb_n, pedido->payload, 4);
            uint32_t nro_bloque = ntohl(nb_n);

            log_info(logger, "## Lectura del bloque: %u", nro_bloque);

            uint8_t* buf = malloc(block_size);
            fseek(swap_file, nro_bloque * block_size, SEEK_SET);
            fread(buf, 1, block_size, swap_file);

            enviar_mensaje(fd, MSG_SWAP_LEER_RESP, buf, block_size);
            free(buf);
        }

        else if (pedido->op_code == MSG_SWAP_ESCRIBIR) {
            uint32_t nb_n; memcpy(&nb_n, pedido->payload, 4);
            uint32_t nro_bloque = ntohl(nb_n);
            uint8_t* datos = (uint8_t*)pedido->payload + 4;

            log_info(logger, "## Escritura del bloque: %u", nro_bloque);

            fseek(swap_file, nro_bloque * block_size, SEEK_SET);
            fwrite(datos, 1, block_size, swap_file);
            fflush(swap_file);

            enviar_mensaje(fd, MSG_OK, NULL, 0);
        }

        else {
            log_warning(logger, "op_code desconocido: %d", pedido->op_code);
            enviar_mensaje(fd, MSG_ERROR, NULL, 0);
        }

        free_mensaje(pedido);
    }

    fclose(swap_file);
    config_destroy(config);
    log_destroy(logger);
    return EXIT_SUCCESS;
}
