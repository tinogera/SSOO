#include "ks_cmn.h"
#include <string.h>

int parsear_queues_algorithms(const char* str, char out[][8]) {
    char buf[256];
    strncpy(buf, str, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    int n = 0;
    char* p = buf;
    if (*p == '[') p++;
    char* tok = strtok(p, ",]");
    while (tok && n < MAX_COLAS) {
        while (*tok == ' ') tok++;
        strncpy(out[n], tok, 7);
        out[n][7] = '\0';
        n++;
        tok = strtok(NULL, ",]");
    }
    return n > 0 ? n : 1;
}
