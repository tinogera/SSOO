#ifndef CPU_SYSCALLS_H_
#define CPU_SYSCALLS_H_

#include <stdbool.h>
#include <stdint.h>
#include <commons/log.h>

// Todas estas funciones arman el payload y lo mandan a KS; no ejecutan la
// operación final. INIT_PROC es la única que espera un OK inmediato.
bool enviar_syscall_mutex_create(int socket_kernel, uint32_t pid, const char* nombre, t_log* logger);
bool enviar_syscall_mutex_lock(int socket_kernel, uint32_t pid, const char* nombre, t_log* logger);
bool enviar_syscall_mutex_unlock(int socket_kernel, uint32_t pid, const char* nombre, t_log* logger);
bool enviar_syscall_sleep(int socket_kernel, uint32_t pid, uint32_t tiempo_ms, t_log* logger);
bool enviar_syscall_stdout(int socket_kernel, uint32_t pid, uint32_t direccion_logica, uint32_t tamanio, t_log* logger);
bool enviar_syscall_stdin(int socket_kernel, uint32_t pid, uint32_t direccion_logica, uint32_t tamanio, t_log* logger);
bool enviar_syscall_mem_alloc(int socket_kernel, uint32_t pid, uint32_t id_segmento, uint32_t tamanio, t_log* logger);
bool enviar_syscall_mem_free(int socket_kernel, uint32_t pid, uint32_t id_segmento, t_log* logger);
bool enviar_syscall_init_proc(int socket_kernel, uint32_t pid, const char* archivo, uint32_t prioridad, t_log* logger);
bool enviar_syscall_exit(int socket_kernel, uint32_t pid, t_log* logger);

#endif
