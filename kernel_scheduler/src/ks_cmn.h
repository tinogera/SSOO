#ifndef KS_CMN_H_
#define KS_CMN_H_

#define MAX_COLAS 16

/*
 * Parsea la cadena QUEUES_ALGORITHMS del config, con formato "[FIFO,RR,RR,FIFO]",
 * y carga en out[][] el algoritmo de cada cola (null-terminated, max 7 chars).
 * Retorna la cantidad de colas parseadas (>= 1).
 */
int parsear_queues_algorithms(const char* str, char out[][8]);

#endif
