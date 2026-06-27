#ifndef _SYS_PTRACE_H
#define _SYS_PTRACE_H

#include <sys/types.h>

/*
 * ptrace(2) — process inspection & control (userspace view).
 *
 * Request numbers follow the Linux/i386 ptrace encoding.  See the kernel
 * implementation in sys/kern/ptrace.c.  The libc wrapper re-exposes the
 * classic PEEK convention: PTRACE_PEEK* returns the read word as the return
 * value (clear errno before the call to disambiguate a word of -1).
 */
#define PTRACE_TRACEME      0
#define PTRACE_PEEKTEXT     1
#define PTRACE_PEEKDATA     2
#define PTRACE_PEEKUSER     3
#define PTRACE_POKETEXT     4
#define PTRACE_POKEDATA     5
#define PTRACE_POKEUSER     6
#define PTRACE_CONT         7
#define PTRACE_KILL         8
#define PTRACE_SINGLESTEP   9
#define PTRACE_GETREGS      12
#define PTRACE_SETREGS      13
#define PTRACE_ATTACH       16
#define PTRACE_DETACH       17

struct user_regs_struct {
    unsigned int ebx, ecx, edx, esi, edi, ebp, eax;
    unsigned int xds, xes, xfs, xgs, orig_eax;
    unsigned int eip, xcs, eflags, esp, xss;
};

long ptrace(int request, pid_t pid, void *addr, void *data);

#endif /* _SYS_PTRACE_H */
