/*
 * Generic syscall header.
 * Redirects to the architecture-specific syscall definitions.
 */

#ifndef _SYS_SYSCALL_H
#define _SYS_SYSCALL_H

/* The system-call number table is one for both architectures: the x86_64
 * kernel serves it to its 32-bit processes, and a 64-bit program uses the
 * same numbers (docs/specs/abi-amd64.md, section 3). */
#if defined(__i386__) || defined(__x86_64__)
#include <machine/syscall.h>
#else
#error "Unsupported architecture for syscalls"
#endif

#endif /* _SYS_SYSCALL_H */
