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

#include "../src/cpu_memoria.h"
#include "../src/cpu_memory_sticks.h"
#include <utils/protocolo.h>
#include <utils/sockets.h>

typedef struct {
    int fd;
    uint32_t id_memory_stick;
    uint32_t puerto;
    int consultas;
    bool protocolo_ok;
    bool disponible;
} t_km_falso;

typedef struct {
    int servidor;
    uint32_t cpu_id;
    bool protocolo_ok;
    bool escritura_ok;
    bool lectura_ok;
    uint8_t memoria[16];
} t_ms_falso;

static void configurar_timeout(int fd) {
    struct timeval timeout = { .tv_sec = 2, .tv_usec = 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
}

static int crear_servidor_local(uint32_t* puerto) {
    int servidor = crear_servidor(0);
    if (servidor < 0) return -1;

    struct sockaddr_in direccion;
    socklen_t largo = sizeof(direccion);
    if (getsockname(servidor, (struct sockaddr*)&direccion, &largo) < 0) {
        close(servidor);
        return -1;
    }
    *puerto = ntohs(direccion.sin_port);
    return servidor;
}

static void* atender_km_falso(void* arg) {
    t_km_falso* km = arg;
    t_mensaje* pedido = recibir_mensaje(km->fd);
    if (pedido == NULL || pedido->op_code != MSG_SOLICITAR_MEMORY_STICK ||
        pedido->payload_size != sizeof(t_payload_solicitar_memory_stick)) {
        km->protocolo_ok = false;
        free_mensaje(pedido);
        return NULL;
    }

    t_payload_solicitar_memory_stick solicitud;
    memcpy(&solicitud, pedido->payload, sizeof(solicitud));
    if (ntohl(solicitud.id_memory_stick) != km->id_memory_stick)
        km->protocolo_ok = false;
    free_mensaje(pedido);
    km->consultas++;

    if (!km->disponible) {
        enviar_mensaje(km->fd, MSG_ERROR, NULL, 0);
        return NULL;
    }

    t_payload_memory_stick_endpoint endpoint = {
        .id_memory_stick = htonl(km->id_memory_stick),
        .ipv4 = htonl(INADDR_LOOPBACK),
        .puerto = htonl(km->puerto)
    };
    enviar_mensaje(km->fd, MSG_MEMORY_STICK_ENDPOINT, &endpoint, sizeof(endpoint));
    return NULL;
}

static void* atender_ms_falso(void* arg) {
    t_ms_falso* ms = arg;
    ms->protocolo_ok = false;
    int cpu = accept(ms->servidor, NULL, NULL);
    if (cpu < 0) return NULL;
    configurar_timeout(cpu);

    t_mensaje* identificacion = recibir_mensaje(cpu);
    if (identificacion != NULL && identificacion->op_code == MSG_CPU_IDENTIFICACION &&
        identificacion->payload_size == sizeof(uint32_t)) {
        uint32_t cpu_id_n;
        memcpy(&cpu_id_n, identificacion->payload, sizeof(cpu_id_n));
        ms->cpu_id = ntohl(cpu_id_n);
        ms->protocolo_ok = true;
        enviar_mensaje(cpu, MSG_OK, NULL, 0);
    }
    free_mensaje(identificacion);

    if (!ms->protocolo_ok) {
        close(cpu);
        close(ms->servidor);
        return NULL;
    }

    t_mensaje* escritura = recibir_mensaje(cpu);
    if (escritura != NULL && escritura->op_code == MSG_MEMORY_WRITE &&
        escritura->payload_size >= sizeof(t_payload_escribir_memoria)) {
        uint32_t direccion_n;
        uint32_t tamanio_n;
        memcpy(&direccion_n, escritura->payload, sizeof(direccion_n));
        memcpy(&tamanio_n, (uint8_t*)escritura->payload + sizeof(direccion_n), sizeof(tamanio_n));
        uint32_t direccion = ntohl(direccion_n);
        uint32_t tamanio = ntohl(tamanio_n);
        if (direccion <= sizeof(ms->memoria) &&
            tamanio <= sizeof(ms->memoria) - direccion &&
            escritura->payload_size == sizeof(t_payload_escribir_memoria) + tamanio) {
            memcpy(ms->memoria + direccion,
                   (uint8_t*)escritura->payload + sizeof(t_payload_escribir_memoria), tamanio);
            ms->escritura_ok = true;
            enviar_mensaje(cpu, MSG_OK, NULL, 0);
        }
    }
    if (!ms->escritura_ok)
        enviar_mensaje(cpu, MSG_ERROR, NULL, 0);
    free_mensaje(escritura);

    t_mensaje* lectura = recibir_mensaje(cpu);
    if (lectura != NULL && lectura->op_code == MSG_MEMORY_READ &&
        lectura->payload_size == sizeof(t_payload_leer_memoria)) {
        t_payload_leer_memoria pedido;
        memcpy(&pedido, lectura->payload, sizeof(pedido));
        uint32_t direccion = ntohl(pedido.dir_fisica);
        uint32_t tamanio = ntohl(pedido.tamanio);
        if (direccion <= sizeof(ms->memoria) && tamanio <= sizeof(ms->memoria) - direccion) {
            ms->lectura_ok = true;
            enviar_mensaje(cpu, MSG_MEMORY_READ_RESPUESTA, ms->memoria + direccion, tamanio);
        }
    }
    if (!ms->lectura_ok)
        enviar_mensaje(cpu, MSG_ERROR, NULL, 0);
    free_mensaje(lectura);

    t_mensaje* extra = recibir_mensaje(cpu);
    if (extra != NULL) {
        ms->protocolo_ok = false;
        free_mensaje(extra);
    }
    close(cpu);
    close(ms->servidor);
    return NULL;
}

context(cpu_memory_sticks) {

    describe("descubrimiento dinamico") {

        it("descubre y conecta un stick que aparece despues de iniciar la CPU") {
            int sockets_km[2] = {-1, -1};
            int socketpair_ok = socketpair(AF_UNIX, SOCK_STREAM, 0, sockets_km);
            should_int(socketpair_ok) be equal to(0);
            if (socketpair_ok != 0) return;

            configurar_timeout(sockets_km[0]);
            configurar_timeout(sockets_km[1]);

            t_km_falso km = {
                .fd = sockets_km[1],
                .id_memory_stick = 12,
                .puerto = 0,
                .consultas = 0,
                .protocolo_ok = true,
                .disponible = false
            };

            t_cpu_memory_sticks registro;
            cpu_memory_sticks_inicializar(&registro, sockets_km[0], 7, NULL);

            pthread_t hilo_km_antes;
            int hilo_km_antes_ok = pthread_create(&hilo_km_antes, NULL, atender_km_falso, &km);
            should_int(hilo_km_antes_ok) be equal to(0);
            if (hilo_km_antes_ok != 0) {
                cpu_memory_sticks_destruir(&registro);
                close(sockets_km[0]);
                close(sockets_km[1]);
                return;
            }

            int antes_del_alta = cpu_memory_sticks_obtener_socket(&registro, 12);
            pthread_join(hilo_km_antes, NULL);
            should_int(antes_del_alta) be equal to(-1);

            uint32_t puerto = 0;
            int servidor_ms = crear_servidor_local(&puerto);
            should_bool(servidor_ms >= 0) be truthy;
            if (servidor_ms < 0) {
                cpu_memory_sticks_destruir(&registro);
                close(sockets_km[0]);
                close(sockets_km[1]);
                return;
            }
            configurar_timeout(servidor_ms);

            km.puerto = puerto;
            km.disponible = true;
            t_ms_falso ms = {
                .servidor = servidor_ms,
                .cpu_id = 0,
                .protocolo_ok = false,
                .escritura_ok = false,
                .lectura_ok = false,
                .memoria = {0}
            };

            pthread_t hilo_ms;
            pthread_t hilo_km_alta;
            int hilo_ms_ok = pthread_create(&hilo_ms, NULL, atender_ms_falso, &ms);
            should_int(hilo_ms_ok) be equal to(0);
            if (hilo_ms_ok != 0) {
                close(servidor_ms);
                cpu_memory_sticks_destruir(&registro);
                close(sockets_km[0]);
                close(sockets_km[1]);
                return;
            }
            int hilo_km_alta_ok = pthread_create(&hilo_km_alta, NULL, atender_km_falso, &km);
            should_int(hilo_km_alta_ok) be equal to(0);
            if (hilo_km_alta_ok != 0) {
                int desbloqueo = conectar_a_servidor("127.0.0.1", (int)puerto);
                if (desbloqueo >= 0) close(desbloqueo);
                pthread_join(hilo_ms, NULL);
                cpu_memory_sticks_destruir(&registro);
                close(sockets_km[0]);
                close(sockets_km[1]);
                return;
            }

            int despues_del_alta = cpu_memory_sticks_obtener_socket(&registro, 12);
            pthread_join(hilo_km_alta, NULL);

            uint8_t esperado[] = {0xde, 0xad, 0xbe, 0xef};
            uint8_t leido[sizeof(esperado)] = {0};
            bool escritura_exitosa = memoria_write(
                despues_del_alta, 3, sizeof(esperado), esperado
            );
            bool lectura_exitosa = memoria_read(
                despues_del_alta, 3, sizeof(leido), leido
            );
            int socket_cacheado = cpu_memory_sticks_obtener_socket(&registro, 12);

            should_bool(despues_del_alta >= 0) be truthy;
            should_bool(escritura_exitosa) be truthy;
            should_bool(lectura_exitosa) be truthy;
            should_int(memcmp(esperado, leido, sizeof(esperado))) be equal to(0);
            should_int(socket_cacheado) be equal to(despues_del_alta);

            cpu_memory_sticks_destruir(&registro);
            pthread_join(hilo_ms, NULL);
            close(sockets_km[0]);
            close(sockets_km[1]);

            should_int(km.consultas) be equal to(2);
            should_bool(km.protocolo_ok) be truthy;
            should_bool(ms.protocolo_ok) be truthy;
            should_bool(ms.escritura_ok) be truthy;
            should_bool(ms.lectura_ok) be truthy;
            should_int(ms.cpu_id) be equal to(7);
        } end

    } end

}
