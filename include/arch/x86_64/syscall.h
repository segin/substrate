#ifndef _ARCH_SYSCALL_H
#define _ARCH_SYSCALL_H

#include <stdint.h>
// Syscall MSRs
#define MSR_STAR      0xC0000081
#define MSR_LSTAR     0xC0000082
#define MSR_FMASK     0xC0000084
#define MSR_KERNEL_GS_BASE 0xC0000102

// x86_64 Syscall Entry (Assembly)
extern void syscall_entry(void);

// C Handler
void syscall_handler_64(uint64_t syscall_number, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5, uint64_t arg6);

// Initialize MSRs for syscall/sysret
void syscall_init_64(void);

#endif
