#include "cpu_registros.h"

#include <string.h>

void inicializar_registros_cpu(t_registros_cpu* registros) {
    memset(registros, 0, sizeof(t_registros_cpu));
}

bool leer_valor_registro_cpu(t_registros_cpu* registros, const char* nombre, uint32_t* valor) {
    if (strcmp(nombre, "AX") == 0)  { *valor = registros->ax;  return true; }
    if (strcmp(nombre, "BX") == 0)  { *valor = registros->bx;  return true; }
    if (strcmp(nombre, "CX") == 0)  { *valor = registros->cx;  return true; }
    if (strcmp(nombre, "DX") == 0)  { *valor = registros->dx;  return true; }
    if (strcmp(nombre, "EAX") == 0) { *valor = registros->eax; return true; }
    if (strcmp(nombre, "EBX") == 0) { *valor = registros->ebx; return true; }
    if (strcmp(nombre, "ECX") == 0) { *valor = registros->ecx; return true; }
    if (strcmp(nombre, "EDX") == 0) { *valor = registros->edx; return true; }
    if (strcmp(nombre, "SI") == 0)  { *valor = registros->si;  return true; }
    if (strcmp(nombre, "DI") == 0)  { *valor = registros->di;  return true; }
    if (strcmp(nombre, "PC") == 0)  { *valor = registros->pc;  return true; }
    return false;
}
