#include <utils/hello.h>
#include <stdio.h>
#include <stdlib.h>

#include <commons/log.h>
#include <commons/config.h>

#include <utils/sockets.h>
#include <utils/protocolo.h>

t_log* logger;


// <>
int main(int argc, char* argv[]) {
    
    //VARIABLES GLOBALES
    int puerto, id, delay, kernel_port;
    char* ip,kernel_ip;

    // -------------------------------------------------------------------
    // 1. Validar argumentos
    // -------------------------------------------------------------------
    if (argc < 3)
    {
        fprintf(stderr, "Cantidad de argumentos invalida.\n");
        fprintf(stderr, "Uso: %s [Archivo Config] [Tamaño]\n", argv[0]);
        return EXIT_FAILURE;
    }

    char* config_path = argv[1];
    char* tamanio = argv[2];
    
    // -------------------------------------------------------------------
    // 2. Leer configuración y cargar variables
    // -------------------------------------------------------------------

    t_config* config = config_create(config_path);
    if (config == NULL) {
        fprintf(stderr, "No se pudo leer el archivo de configuración: %s\n", config_path);
        return EXIT_FAILURE;
    }else{
        puerto = config_get_int_value(config, "MEMORY_STICK_PORT");
        id = config_get_int_value(config, "MEMORY_STICK_ID");
        ip = config_get_string_value(config, "MEMORY_STICK_IP");
        //log_level = config_get_string_value(config, "LOG_LEVEL");
        delay = config_get_int_value(config, "MEMORY_DELAY");
        kernel_port = config_get_int_value(config, "KERNEL_MEMORY_PORT");
        kernel_ip = config_get_string_value(config, "KERNEL_MEMORY_IP");
    }

    // -------------------------------------------------------------------
    // 3. Inicializar logger
    // -------------------------------------------------------------------

    char log_file[64];
    snprintf(log_file, sizeof(log_file), "MemoryStick_%i.log", id);

    logger = log_create(log_file, "IO", true, LOG_LEVEL_INFO);
    if (logger == NULL) {
        fprintf(stderr, "Error al crear el logger\n");
        return EXIT_FAILURE;
    }

    // IMPRIMIR LOGS DE INICIO (DEBUG)

    log_info(logger, "Configuración cargada desde %s\n", config_path);
    log_info(logger, "Memory Stick ID: %i\n", id);
    log_info(logger, "IP: %s\n", ip);
    log_info(logger, "Puerto: %i\n", puerto);
    log_info(logger, "Memory Delay: %i\n", delay);
    log_info(logger, "Archivo log creado en: %s\n", log_file);

    // -------------------------------------------------------------------
    // 4. Conectarse al Kernel Memory
    // -------------------------------------------------------------------

    if (kernel_ip == NULL) {
        log_error(logger, "Falta KERNEL_MEMORY_IP en el archivo de configuración\n");
        config_destroy(config);
        log_destroy(logger);
        return EXIT_FAILURE;
    }

    // -------------------------------------------------------------------
    // 5. Conectarse a Kernel Memory
    // -------------------------------------------------------------------

    int fd = conectar_a_servidor(kernel_ip, kernel_port);
    if (fd < 0) {
        log_error(logger, "Kernel Memory no esta levantado o los datos son incorrectos\n");
    }
    log_info(logger, "## Conectado a Kernel Memory\n");
    // -------------------------------------------------------------------
    // 6. Envio de datos propios a kernel memory 
    //    FORMATO: "MEMORYSTICK IP, MEMORYSTICK PORT"
    // -------------------------------------------------------------------
    char* msdatos;
    snprintf(msdatos, sizeof(msdatos), "%s", ip);
    snprintf(msdatos, sizeof(msdatos), ", %s", puerto);
    uint32_t size;
    void* payload = serializar_string(msdatos, &size);
    enviar_mensaje(fd, MSG_IO_IDENTIFICACION, payload, size);
    free(payload);

    // -------------------------------------------------------------------
    // 7. Creo servidor y se qued a la espera de una CPU
    // -------------------------------------------------------------------
    int fd_servidor = crear_servidor(puerto);
    if (fd_servidor < 0) {
        log_error(logger, "Error levantando servidor\n");
    }
    log_info(logger, "Se levanto servidor para esperar un CPU, puerto: %s\n", puerto);
    int fd_cliente = aceptar_conexion(fd_servidor);
    log_info(logger, "## CPU <%s> Conectada\n", fd_cliente);

    

    config_destroy(config);
    log_destroy(logger);
    return EXIT_SUCCESS;
    return 0;
}
