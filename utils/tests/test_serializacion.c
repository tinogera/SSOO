#include <cspecs/cspec.h>
#include <utils/sockets.h>
#include <string.h>
#include <stdlib.h>

context (serializacion) {

    describe ("serializar_string") {

        it ("incluye el caracter nulo") {
            uint32_t size;
            void* buf = serializar_string("hola", &size);
            should_int(size) be equal to(5);
            free(buf);
        } end

        it ("el contenido es igual al string original") {
            uint32_t size;
            void* buf = serializar_string("hola", &size);
            should_int(memcmp(buf, "hola", 5)) be equal to(0);
            free(buf);
        } end

        it ("un string vacío serializa con tamaño 1 (solo el nulo)") {
            uint32_t size;
            void* buf = serializar_string("", &size);
            should_int(size) be equal to(1);
            free(buf);
        } end

        it ("no retorna NULL") {
            uint32_t size;
            void* buf = serializar_string("test", &size);
            should_ptr(buf) not be null;
            free(buf);
        } end

    } end

    describe ("deserializar_string") {

        it ("retorna el string original") {
            uint32_t size;
            void* buf = serializar_string("kernel", &size);
            char* result = deserializar_string(buf);
            should_string(result) be equal to("kernel");
            free(buf);
            free(result);
        } end

        it ("no retorna NULL") {
            uint32_t size;
            void* buf = serializar_string("io", &size);
            char* result = deserializar_string(buf);
            should_ptr(result) not be null;
            free(buf);
            free(result);
        } end

    } end

    describe ("roundtrip serializar/deserializar") {

        it ("recupera el string original") {
            char* original = "SLEEP";
            uint32_t size;
            void* buf = serializar_string(original, &size);
            char* recuperado = deserializar_string(buf);
            should_string(recuperado) be equal to(original);
            free(buf);
            free(recuperado);
        } end

        it ("funciona con los tres tipos de IO") {
            char* tipos[] = {"STDIN", "STDOUT", "SLEEP"};
            for (int i = 0; i < 3; i++) {
                uint32_t size;
                void* buf = serializar_string(tipos[i], &size);
                char* result = deserializar_string(buf);
                should_string(result) be equal to(tipos[i]);
                free(buf);
                free(result);
            }
        } end

    } end

    describe ("free_mensaje") {

        it ("no falla con NULL") {
            free_mensaje(NULL);
            should_bool(true) be truthy;
        } end

        it ("no falla con payload NULL") {
            t_mensaje* msg = malloc(sizeof(t_mensaje));
            msg->op_code      = 0;
            msg->payload_size = 0;
            msg->payload      = NULL;
            free_mensaje(msg);
            should_bool(true) be truthy;
        } end

    } end

} end
