#ifndef CPU_SYSCALLS_H_
#define CPU_SYSCALLS_H_

#include <stdbool.h>
#include <stdint.h>
#include <commons/log.h>

bool enviar_syscall_mutex_create(int socket_kernel, uint32_t pid, const char* nombre, t_log* logger);
bool enviar_syscall_mutex_lock(int socket_kernel, uint32_t pid, const char* nombre, t_log* logger);
bool enviar_syscall_mutex_unlock(int socket_kernel, uint32_t pid, const char* nombre, t_log* logger);
bool enviar_syscall_sleep(int socket_kernel, uint32_t pid, uint32_t tiempo_ms, t_log* logger);
bool enviar_syscall_stdout(int socket_kernel, uint32_t pid, uint32_t direccion_logica, uint32_t tamanio, t_log* logger);
bool enviar_syscall_stdin(int socket_kernel, uint32_t pid, uint32_t direccion_logica, uint32_t tamanio, t_log* logger);
bool enviar_syscall_exit(int socket_kernel, uint32_t pid, t_log* logger);

#endif
