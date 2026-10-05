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

#if defined(__x86_64__)
/* A 64-bit tracer's register set: the Linux/amd64 layout.  PEEK and POKE
 * move one long (8 bytes) at a time. */
struct user_regs_struct {
    unsigned long long r15, r14, r13, r12, rbp, rbx, r11, r10;
    unsigned long long r9, r8, rax, rcx, rdx, rsi, rdi, orig_rax;
    unsigned long long rip, cs, eflags, rsp, ss;
    unsigned long long fs_base, gs_base;
    unsigned long long ds, es, fs, gs;
};
#else
struct user_regs_struct {
    unsigned int ebx, ecx, edx, esi, edi, ebp, eax;
    unsigned int xds, xes, xfs, xgs, orig_eax;
    unsigned int eip, xcs, eflags, esp, xss;
};
#endif

#ifdef __cplusplus
extern "C" {
#endif

long ptrace(int request, pid_t pid, void *addr, void *data);

#ifdef __cplusplus
}
#endif

#endif /* _SYS_PTRACE_H */
