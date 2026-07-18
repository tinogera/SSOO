#include "cpu_registros.h"

#include <string.h>

/*
 * Centralicé el acceso a registros para no repetir casts y comparaciones en
 * cada instrucción. AX/BX/CX/DX son de 8 bits; el resto y PC son de 32 bits.
 */
void inicializar_registros_cpu(t_registros_cpu* registros) {
    // Al poner toda la estructura en cero también dejo PC en la instrucción 0.
    memset(registros, 0, sizeof(t_registros_cpu));
}

bool leer_valor_registro_cpu(t_registros_cpu* registros, const char* nombre, uint32_t* valor) {
    // Devuelvo todo como uint32_t para que execute tenga una interfaz única.
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

bool escribir_valor_registro_cpu(t_registros_cpu* registros, const char* nombre, uint32_t valor) {
    uint32_t tamanio;
    if (!tamanio_registro_cpu(nombre, &tamanio)) {
        return false;
    }

    if (tamanio == 1) {
        // El cast hace el truncamiento natural. Por ejemplo, 300 en AX queda 44.
        uint8_t valor_8 = (uint8_t) valor;

        if (strcmp(nombre, "AX") == 0) { registros->ax = valor_8; return true; }
        if (strcmp(nombre, "BX") == 0) { registros->bx = valor_8; return true; }
        if (strcmp(nombre, "CX") == 0) { registros->cx = valor_8; return true; }
        if (strcmp(nombre, "DX") == 0) { registros->dx = valor_8; return true; }
    }

    if (strcmp(nombre, "EAX") == 0) { registros->eax = valor; return true; }
    if (strcmp(nombre, "EBX") == 0) { registros->ebx = valor; return true; }
    if (strcmp(nombre, "ECX") == 0) { registros->ecx = valor; return true; }
    if (strcmp(nombre, "EDX") == 0) { registros->edx = valor; return true; }
    if (strcmp(nombre, "SI") == 0)  { registros->si = valor;  return true; }
    if (strcmp(nombre, "DI") == 0)  { registros->di = valor;  return true; }
    if (strcmp(nombre, "PC") == 0)  { registros->pc = valor;  return true; }

    return false;
}

bool tamanio_registro_cpu(const char* nombre, uint32_t* tamanio) {
    // Este tamaño también define cuántos bytes leen/escriben MOV_IN y MOV_OUT.
    if (
        strcmp(nombre, "AX") == 0 ||
        strcmp(nombre, "BX") == 0 ||
        strcmp(nombre, "CX") == 0 ||
        strcmp(nombre, "DX") == 0
    ) {
        *tamanio = 1;
        return true;
    }

    if (
        strcmp(nombre, "EAX") == 0 ||
        strcmp(nombre, "EBX") == 0 ||
        strcmp(nombre, "ECX") == 0 ||
        strcmp(nombre, "EDX") == 0 ||
        strcmp(nombre, "SI") == 0 ||
        strcmp(nombre, "DI") == 0 ||
        strcmp(nombre, "PC") == 0
    ) {
        *tamanio = 4;
        return true;
    }

    return false;
}
