#include "io_utils.h"
#include <string.h>

int es_tipo_valido(const char* tipo) {
    return strcmp(tipo, "STDIN")  == 0 ||
           strcmp(tipo, "STDOUT") == 0 ||
           strcmp(tipo, "SLEEP")  == 0;
}
