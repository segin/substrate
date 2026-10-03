/*
 * Generic syscall header.
 * Redirects to the architecture-specific syscall definitions.
 */

#ifndef _SYS_SYSCALL_H
#define _SYS_SYSCALL_H

/* The x86_64 kernel defines __x86_64__ too, but serves 32-bit processes:
 * it wants <machine/syscall.h>, not the 64-bit host header the host tests
 * get below. */
#if defined(__i386__) || defined(SUBSTRATE_ARCH_X86_64)
#include <machine/syscall.h>
#elif defined(__x86_64__)
#include <arch/x86_64/syscall.h>
#else
#error "Unsupported architecture for syscalls"
#endif

#endif /* _SYS_SYSCALL_H */
