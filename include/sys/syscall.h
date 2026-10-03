/*
 * Generic syscall header.
 * Redirects to the system-call number table.
 */

#ifndef _SYS_SYSCALL_H
#define _SYS_SYSCALL_H

/*
 * The native system-call numbers are one table for both architectures
 * (docs/specs/abi-amd64.md, section 3): a 64-bit program uses the i386
 * numbers and differs only in how it passes arguments.
 */
#if defined(__i386__) || defined(__x86_64__)
#include <arch/i386/syscall.h>
#else
#error "Unsupported architecture for syscalls"
#endif

#endif /* _SYS_SYSCALL_H */
