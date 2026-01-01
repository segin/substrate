#ifndef _ARCH_SYSCALL_H
#define _ARCH_SYSCALL_H

#include <stdint.h>
#include "../../kern/personality.h"

// x86_64 Syscall Entry (Assembly)
extern void syscall_entry(void);

// C Handler
void syscall_handler_64(uint64_t syscall_number, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5, uint64_t arg6);

// Initialize MSRs for syscall/sysret
void syscall_init_64(void);

#endif
