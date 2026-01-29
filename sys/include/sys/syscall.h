/*
 * Generic syscall header.
 * Redirects to the architecture-specific syscall definitions.
 */

#ifndef _SYS_SYSCALL_H
#define _SYS_SYSCALL_H

#if defined(__i386__)
#include <arch/i386/syscall.h>
#elif defined(__x86_64__)
#include <arch/x86_64/syscall.h>
#else
#error "Unsupported architecture for syscalls"
#endif

#endif /* _SYS_SYSCALL_H */
