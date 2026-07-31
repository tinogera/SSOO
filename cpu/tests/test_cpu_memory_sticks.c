#include <arpa/inet.h>
#include <cspecs/cspec.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "../src/cpu_decode.h"
#include "../src/cpu_memoria.h"
#include "../src/cpu_memory_sticks.h"
#include "../src/cpu_mmu.h"
#include "../src/cpu_registros.h"
#include <utils/protocolo.h>
#include <utils/sockets.h>

typedef struct {
    int fd;
    uint32_t id_primero;
    uint32_t id_segundo;
    uint32_t puerto_primero;
    uint32_t puerto_segundo;
    bool protocolo_ok;
} t_km_sticks_falso;

typedef struct {
    int servidor;
    uint32_t base_global;
    uint8_t memoria[8];
    uint32_t direccion_write;
    uint32_t tamanio_write;
    uint32_t direccion_read;
    uint32_t tamanio_read;
    bool protocolo_ok;
} t_ms_fragmento_falso;

static void configurar_timeout_ms(int fd) {
    struct timeval timeout = { .tv_sec = 2, .tv_usec = 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
}

static int crear_servidor_local_ms(uint32_t* puerto) {
    int servidor = crear_servidor(0);
    if (servidor < 0) {
        return -1;
    }
    struct sockaddr_in direccion;
    socklen_t largo = sizeof(direccion);
    if (getsockname(servidor, (struct sockaddr*) &direccion, &largo) < 0) {
        close(servidor);
        return -1;
    }
    *puerto = ntohs(direccion.sin_port);
    configurar_timeout_ms(servidor);
    return servidor;
}

static void enviar_endpoint_falso(
    int fd,
    uint32_t id,
    uint32_t puerto,
    uint32_t base
) {
    t_payload_memory_stick_endpoint endpoint = {
        .id_memory_stick = htonl(id),
        .ipv4 = htonl(INADDR_LOOPBACK),
        .puerto = htonl(puerto),
        .base_global = htonl(base),
        .tamanio = htonl(8)
    };
    enviar_mensaje(fd, MSG_MEMORY_STICK_ENDPOINT, &endpoint, sizeof(endpoint));
}

static void* atender_km_sticks_falso(void* arg) {
    t_km_sticks_falso* km = arg;
    km->protocolo_ok = true;

    t_mensaje* por_id = recibir_mensaje(km->fd);
    if (por_id == NULL || por_id->op_code != MSG_SOLICITAR_MEMORY_STICK ||
        por_id->payload_size != sizeof(t_payload_solicitar_memory_stick)) {
        km->protocolo_ok = false;
        free_mensaje(por_id);
        return NULL;
    }
    t_payload_solicitar_memory_stick solicitud_id;
    memcpy(&solicitud_id, por_id->payload, sizeof(solicitud_id));
    if (ntohl(solicitud_id.id_memory_stick) != km->id_primero) {
        km->protocolo_ok = false;
    }
    free_mensaje(por_id);
    enviar_endpoint_falso(km->fd, km->id_primero, km->puerto_primero, 0);

    t_mensaje* por_direccion = recibir_mensaje(km->fd);
    if (por_direccion == NULL ||
        por_direccion->op_code != MSG_SOLICITAR_MEMORY_STICK_DIRECCION ||
        por_direccion->payload_size != sizeof(t_payload_solicitar_memory_stick_direccion)) {
        km->protocolo_ok = false;
        free_mensaje(por_direccion);
        return NULL;
    }
    t_payload_solicitar_memory_stick_direccion solicitud_direccion;
    memcpy(&solicitud_direccion, por_direccion->payload, sizeof(solicitud_direccion));
    if (ntohl(solicitud_direccion.direccion_fisica) != 8) {
        km->protocolo_ok = false;
    }
    free_mensaje(por_direccion);
    enviar_endpoint_falso(km->fd, km->id_segundo, km->puerto_segundo, 8);
    return NULL;
}

static bool recibir_write_fragmento(int fd, t_ms_fragmento_falso* ms) {
    t_mensaje* pedido = recibir_mensaje(fd);
    if (pedido == NULL || pedido->op_code != MSG_MEMORY_WRITE ||
        pedido->payload_size < sizeof(t_payload_escribir_memoria)) {
        free_mensaje(pedido);
        return false;
    }

    uint32_t direccion_n;
    uint32_t tamanio_n;
    memcpy(&direccion_n, pedido->payload, sizeof(direccion_n));
    memcpy(
        &tamanio_n,
        (uint8_t*) pedido->payload + sizeof(direccion_n),
        sizeof(tamanio_n)
    );
    ms->direccion_write = ntohl(direccion_n);
    ms->tamanio_write = ntohl(tamanio_n);
    bool valido = ms->direccion_write >= ms->base_global &&
                  ms->direccion_write - ms->base_global <= sizeof(ms->memoria) &&
                  ms->tamanio_write <= sizeof(ms->memoria) -
                                       (ms->direccion_write - ms->base_global) &&
                  pedido->payload_size == sizeof(t_payload_escribir_memoria) + ms->tamanio_write;
    if (valido) {
        memcpy(
            ms->memoria + ms->direccion_write - ms->base_global,
            (uint8_t*) pedido->payload + sizeof(t_payload_escribir_memoria),
            ms->tamanio_write
        );
        enviar_mensaje(fd, MSG_OK, NULL, 0);
    } else {
        enviar_mensaje(fd, MSG_ERROR, NULL, 0);
    }
    free_mensaje(pedido);
    return valido;
}

static bool recibir_read_fragmento(int fd, t_ms_fragmento_falso* ms) {
    t_mensaje* pedido = recibir_mensaje(fd);
    if (pedido == NULL || pedido->op_code != MSG_MEMORY_READ ||
        pedido->payload_size != sizeof(t_payload_leer_memoria)) {
        free_mensaje(pedido);
        return false;
    }

    t_payload_leer_memoria lectura;
    memcpy(&lectura, pedido->payload, sizeof(lectura));
    ms->direccion_read = ntohl(lectura.dir_fisica);
    ms->tamanio_read = ntohl(lectura.tamanio);
    bool valido = ms->direccion_read >= ms->base_global &&
                  ms->direccion_read - ms->base_global <= sizeof(ms->memoria) &&
                  ms->tamanio_read <= sizeof(ms->memoria) -
                                      (ms->direccion_read - ms->base_global);
    if (valido) {
        enviar_mensaje(
            fd,
            MSG_MEMORY_READ_RESPUESTA,
            ms->memoria + ms->direccion_read - ms->base_global,
            ms->tamanio_read
        );
    } else {
        enviar_mensaje(fd, MSG_ERROR, NULL, 0);
    }
    free_mensaje(pedido);
    return valido;
}

static void* atender_ms_fragmento_falso(void* arg) {
    t_ms_fragmento_falso* ms = arg;
    ms->protocolo_ok = false;
    int cpu = accept(ms->servidor, NULL, NULL);
    if (cpu < 0) {
        return NULL;
    }
    configurar_timeout_ms(cpu);

    t_mensaje* identificacion = recibir_mensaje(cpu);
    bool identificacion_ok = identificacion != NULL &&
                             identificacion->op_code == MSG_CPU_IDENTIFICACION &&
                             identificacion->payload_size == sizeof(uint32_t);
    free_mensaje(identificacion);
    if (!identificacion_ok) {
        close(cpu);
        return NULL;
    }
    enviar_mensaje(cpu, MSG_OK, NULL, 0);

    bool write_ok = recibir_write_fragmento(cpu, ms);
    bool read_ok = recibir_read_fragmento(cpu, ms);
    ms->protocolo_ok = write_ok && read_ok;
    close(cpu);
    close(ms->servidor);
    return NULL;
}

context(cpu_memory_sticks) {

    describe("accesos fisicos fragmentados") {

        it("divide y consolida MOV_OUT y MOV_IN entre dos sticks") {
            uint32_t puerto_primero = 0;
            uint32_t puerto_segundo = 0;
            int servidor_primero = crear_servidor_local_ms(&puerto_primero);
            int servidor_segundo = crear_servidor_local_ms(&puerto_segundo);
            should_bool(servidor_primero >= 0) be truthy;
            should_bool(servidor_segundo >= 0) be truthy;
            if (servidor_primero < 0 || servidor_segundo < 0) return;

            t_ms_fragmento_falso primero = {
                .servidor = servidor_primero,
                .base_global = 0,
                .memoria = {0}
            };
            t_ms_fragmento_falso segundo = {
                .servidor = servidor_segundo,
                .base_global = 8,
                .memoria = {0}
            };

            int sockets_km[2] = {-1, -1};
            int socketpair_ok = socketpair(AF_UNIX, SOCK_STREAM, 0, sockets_km);
            should_int(socketpair_ok) be equal to(0);
            if (socketpair_ok != 0) return;
            configurar_timeout_ms(sockets_km[0]);
            configurar_timeout_ms(sockets_km[1]);

            t_km_sticks_falso km = {
                .fd = sockets_km[1],
                .id_primero = 10,
                .id_segundo = 42,
                .puerto_primero = puerto_primero,
                .puerto_segundo = puerto_segundo,
                .protocolo_ok = false
            };

            pthread_t hilo_km;
            pthread_t hilo_primero;
            pthread_t hilo_segundo;
            int hilos_ok = pthread_create(&hilo_km, NULL, atender_km_sticks_falso, &km) |
                           pthread_create(&hilo_primero, NULL, atender_ms_fragmento_falso, &primero) |
                           pthread_create(&hilo_segundo, NULL, atender_ms_fragmento_falso, &segundo);
            should_int(hilos_ok) be equal to(0);
            if (hilos_ok != 0) return;

            t_cpu_memory_sticks registro;
            cpu_memory_sticks_inicializar(&registro, sockets_km[0], 7, NULL);
            t_log* logger = log_create(
                "/tmp/cpu_memory_sticks_test.log",
                "cpu_memory_sticks_test",
                false,
                LOG_LEVEL_ERROR
            );

            t_entrada_segmento segmento = {
                .id_segmento = 0,
                .id_memory_stick = 10,
                .base = 6,
                .limite = 8
            };
            t_contexto contexto = {
                .pid = 3,
                .cant_segmentos = 1,
                .segmentos = &segmento
            };
            t_registros_cpu registros;
            inicializar_registros_cpu(&registros);
            registros.di = 0;
            registros.eax = 77;
            set_segment_max_size(256);

            t_instruccion_decodificada mov_out = decode_instruccion("MOV_OUT EAX");
            t_resultado_memoria_cpu resultado_out = ejecutar_instruccion_memoria(
                &registro,
                &mov_out,
                &contexto,
                &registros,
                3,
                logger
            );

            registros.si = 0;
            t_instruccion_decodificada mov_in = decode_instruccion("MOV_IN PC");
            t_resultado_memoria_cpu resultado_in = ejecutar_instruccion_memoria(
                &registro,
                &mov_in,
                &contexto,
                &registros,
                3,
                logger
            );

            should_int(resultado_out) be equal to(CPU_MEMORIA_OK);
            should_int(resultado_in) be equal to(CPU_MEMORIA_OK);
            should_int(registros.pc) be equal to(77);
            should_int(primero.direccion_write) be equal to(6);
            should_int(primero.tamanio_write) be equal to(2);
            should_int(segundo.direccion_write) be equal to(8);
            should_int(segundo.tamanio_write) be equal to(2);
            should_int(primero.direccion_read) be equal to(6);
            should_int(primero.tamanio_read) be equal to(2);
            should_int(segundo.direccion_read) be equal to(8);
            should_int(segundo.tamanio_read) be equal to(2);

            cpu_memory_sticks_destruir(&registro);
            log_destroy(logger);
            pthread_join(hilo_km, NULL);
            pthread_join(hilo_primero, NULL);
            pthread_join(hilo_segundo, NULL);
            close(sockets_km[0]);
            close(sockets_km[1]);

            should_bool(km.protocolo_ok) be truthy;
            should_bool(primero.protocolo_ok) be truthy;
            should_bool(segundo.protocolo_ok) be truthy;
        } end

    } end

}
