#ifndef LIBSTD_H
#define LIBSTD_H

#include <stdint.h>
#include <stddef.h>
#include "common.h"

/* Userspace and kernel-space data transfer interface (PID based) */
int copy_from_user(uint32_t pid, void *kdst, addr_t usrc, uint32_t size);
int copy_to_user(uint32_t pid, addr_t udst, const void *ksrc, uint32_t size);

/* User-facing system call invocation wrappers */
int sys_call_invoke(int syscall_no, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3);

#endif /* LIBSTD_H */
