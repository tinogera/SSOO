#include <cspecs/cspec.h>
#include "io_utils.h"

context (io_utils) {

    describe ("es_tipo_valido") {

        it ("acepta STDIN") {
            should_bool(es_tipo_valido("STDIN")) be truthy;
        } end

        it ("acepta STDOUT") {
            should_bool(es_tipo_valido("STDOUT")) be truthy;
        } end

        it ("acepta SLEEP") {
            should_bool(es_tipo_valido("SLEEP")) be truthy;
        } end

        it ("rechaza un tipo desconocido") {
            should_bool(es_tipo_valido("FOO")) be falsy;
        } end

        it ("rechaza string vacío") {
            should_bool(es_tipo_valido("")) be falsy;
        } end

        it ("distingue mayúsculas y minúsculas") {
            should_bool(es_tipo_valido("sleep")) be falsy;
            should_bool(es_tipo_valido("Sleep")) be falsy;
        } end

    } end

} end
