#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/file.h>
#include <arpa/inet.h>
#include <commons/log.h>
#include <commons/config.h>
#include <utils/sockets.h>
#include <utils/protocolo.h>

int main(int argc, char* argv[]) {
    int resultado = EXIT_FAILURE;
    int fd = -1;
    int swap_file_fd = -1;
    FILE* swap_file = NULL;
    t_log* logger = NULL;
    t_config* config = NULL;

    if (argc < 2) {
        fprintf(stderr, "Uso: %s [CONFIG]\n", argv[0]);
        return EXIT_FAILURE;
    }

    config = config_create(argv[1]);
    if (!config) {
        fprintf(stderr, "Error leyendo config\n");
        return EXIT_FAILURE;
    }

    const char* claves[] = {
        "LOG_LEVEL", "KERNEL_MEMORY_IP", "KERNEL_MEMORY_PORT",
        "SWAP_FILE_SIZE", "BLOCK_SIZE", "SWAP_FILE_PATH"
    };
    for (size_t i = 0; i < sizeof(claves) / sizeof(claves[0]); i++) {
        if (!config_has_property(config, (char*)claves[i])) {
            fprintf(stderr, "Falta la clave obligatoria %s\n", claves[i]);
            goto cleanup;
        }
    }

    logger = log_create(
        "swap.log",
        "Swap",
        true,
        log_level_from_string(config_get_string_value(config, "LOG_LEVEL"))
    );
    if (!logger) {
        fprintf(stderr, "Error creando logger\n");
        goto cleanup;
    }

    char* ip_km = config_get_string_value(config, "KERNEL_MEMORY_IP");
    int puerto_km = config_get_int_value(config, "KERNEL_MEMORY_PORT");
    int swap_size_config = config_get_int_value(config, "SWAP_FILE_SIZE");
    int block_size_config = config_get_int_value(config, "BLOCK_SIZE");
    char* swap_path = config_get_string_value(config, "SWAP_FILE_PATH");

    if (!ip_km || ip_km[0] == '\0' || puerto_km <= 0 || puerto_km > 65535 ||
        !swap_path || swap_path[0] == '\0' ||
        swap_size_config <= 0 || block_size_config <= 0 ||
        swap_size_config < block_size_config ||
        swap_size_config % block_size_config != 0) {
        log_error(logger, "Configuracion de Swap invalida");
        goto cleanup;
    }

    uint32_t swap_size = (uint32_t)swap_size_config;
    uint32_t block_size = (uint32_t)block_size_config;
    uint32_t cant_bloques = swap_size / block_size;

    swap_file_fd = open(swap_path, O_RDWR | O_CREAT, 0600);
    if (swap_file_fd < 0) {
        log_error(logger, "No se pudo abrir el archivo de swap: %s", swap_path);
        goto cleanup;
    }
    if (flock(swap_file_fd, LOCK_EX | LOCK_NB) != 0) {
        log_error(logger, "El archivo de swap ya esta siendo utilizado");
        goto cleanup;
    }
    if (ftruncate(swap_file_fd, (off_t)swap_size) != 0) {
        log_error(logger, "No se pudo dimensionar el archivo de swap");
        goto cleanup;
    }
    swap_file = fdopen(swap_file_fd, "r+b");
    if (!swap_file) {
        log_error(logger, "No se pudo asociar el archivo de swap");
        goto cleanup;
    }
    swap_file_fd = -1;

    fd = conectar_a_servidor(ip_km, puerto_km);
    if (fd < 0) {
        log_error(logger, "No se pudo conectar a Kernel Memory en %s:%d", ip_km, puerto_km);
        goto cleanup;
    }

    uint8_t payload[8];
    uint32_t ss_n = htonl(swap_size);
    uint32_t bs_n = htonl(block_size);
    memcpy(payload, &ss_n, sizeof(ss_n));
    memcpy(payload + sizeof(ss_n), &bs_n, sizeof(bs_n));
    enviar_mensaje(fd, MSG_SWAP_IDENTIFICACION, payload, sizeof(payload));

    t_mensaje* resp = recibir_mensaje(fd);
    if (!resp || resp->op_code != MSG_OK || resp->payload_size != 0) {
        log_error(logger, "Kernel Memory rechazo la conexion");
        if (resp) free_mensaje(resp);
        goto cleanup;
    }
    free_mensaje(resp);
    log_info(logger, "## Conectado a Kernel Memory");

    t_mensaje* pedido;
    while ((pedido = recibir_mensaje(fd)) != NULL) {
        if (pedido->op_code == MSG_SWAP_LEER) {
            if (pedido->payload_size != sizeof(uint32_t) || !pedido->payload) {
                log_warning(logger, "Pedido de lectura invalido");
                enviar_mensaje(fd, MSG_ERROR, NULL, 0);
                free_mensaje(pedido);
                continue;
            }

            uint32_t nb_n;
            memcpy(&nb_n, pedido->payload, sizeof(nb_n));
            uint32_t nro_bloque = ntohl(nb_n);
            if (nro_bloque >= cant_bloques) {
                log_warning(logger, "Bloque de lectura fuera de rango: %u", nro_bloque);
                enviar_mensaje(fd, MSG_ERROR, NULL, 0);
                free_mensaje(pedido);
                continue;
            }

            uint8_t* buf = malloc(block_size);
            uint64_t offset = (uint64_t)nro_bloque * block_size;
            if (!buf ||
                fseeko(swap_file, (off_t)offset, SEEK_SET) != 0 ||
                fread(buf, 1, block_size, swap_file) != block_size) {
                clearerr(swap_file);
                log_error(logger, "Fallo al leer el bloque %u", nro_bloque);
                enviar_mensaje(fd, MSG_ERROR, NULL, 0);
            } else {
                log_info(logger, "## Lectura del bloque: %u", nro_bloque);
                enviar_mensaje(fd, MSG_SWAP_LEER_RESP, buf, block_size);
            }
            free(buf);
        }
        else if (pedido->op_code == MSG_SWAP_ESCRIBIR) {
            uint32_t payload_esperado = sizeof(uint32_t) + block_size;
            if (pedido->payload_size != payload_esperado || !pedido->payload) {
                log_warning(logger, "Pedido de escritura invalido");
                enviar_mensaje(fd, MSG_ERROR, NULL, 0);
                free_mensaje(pedido);
                continue;
            }

            uint32_t nb_n;
            memcpy(&nb_n, pedido->payload, sizeof(nb_n));
            uint32_t nro_bloque = ntohl(nb_n);
            uint8_t* datos = (uint8_t*)pedido->payload + sizeof(uint32_t);
            if (nro_bloque >= cant_bloques) {
                log_warning(logger, "Bloque de escritura fuera de rango: %u", nro_bloque);
                enviar_mensaje(fd, MSG_ERROR, NULL, 0);
                free_mensaje(pedido);
                continue;
            }

            uint64_t offset = (uint64_t)nro_bloque * block_size;
            if (fseeko(swap_file, (off_t)offset, SEEK_SET) != 0 ||
                fwrite(datos, 1, block_size, swap_file) != block_size ||
                fflush(swap_file) != 0) {
                clearerr(swap_file);
                log_error(logger, "Fallo al escribir el bloque %u", nro_bloque);
                enviar_mensaje(fd, MSG_ERROR, NULL, 0);
            } else {
                log_info(logger, "## Escritura del bloque: %u", nro_bloque);
                enviar_mensaje(fd, MSG_OK, NULL, 0);
            }
        }
        else {
            log_warning(logger, "op_code desconocido: %u", pedido->op_code);
            enviar_mensaje(fd, MSG_ERROR, NULL, 0);
        }

        free_mensaje(pedido);
    }

    log_warning(logger, "Conexion con Kernel Memory finalizada");
    resultado = EXIT_SUCCESS;

cleanup:
    if (fd >= 0) close(fd);
    if (swap_file) fclose(swap_file);
    else if (swap_file_fd >= 0) close(swap_file_fd);
    if (logger) log_destroy(logger);
    if (config) config_destroy(config);
    return resultado;
}
