#include <cspecs/cspec.h>
#include "io_utils.h"

context (io_utils) {

    describe ("es_tipo_valido") {

        it ("acepta STDIN") {
            should_bool(es_tipo_valido("STDIN")) be truthy;
        }

        it ("acepta STDOUT") {
            should_bool(es_tipo_valido("STDOUT")) be truthy;
        }

        it ("acepta SLEEP") {
            should_bool(es_tipo_valido("SLEEP")) be truthy;
        }

        it ("rechaza un tipo desconocido") {
            should_bool(es_tipo_valido("FOO")) be falsy;
        }

        it ("rechaza string vacío") {
            should_bool(es_tipo_valido("")) be falsy;
        }

        it ("distingue mayúsculas y minúsculas") {
            should_bool(es_tipo_valido("sleep")) be falsy;
            should_bool(es_tipo_valido("Sleep")) be falsy;
        }

    }

}
