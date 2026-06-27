#ifndef _SYS_PTRACE_H
#define _SYS_PTRACE_H

/*
 * ptrace(2) request ABI — process inspection & control.
 *
 * Request numbers follow the Linux/i386 ptrace encoding so a Linux-compatible
 * debugger (gdb) sees a familiar interface; the underlying syscall is
 * SYS_PTRACE (26).  Implementation: sys/kern/ptrace.c.
 *
 * Calling conventions for the raw syscall (req, pid, addr, data):
 *   PTRACE_TRACEME              caller asks to be traced by its parent.
 *   PTRACE_ATTACH               stop pid and become its tracer.
 *   PTRACE_PEEKTEXT/PEEKDATA    read one word at `addr`; the word is stored
 *                               through `data` (a uint32_t*).  Returns 0/-errno
 *                               (no in-band -1/word ambiguity — the libc
 *                               wrapper re-exposes the classic "returns word").
 *   PTRACE_POKETEXT/POKEDATA    write the word `data` at `addr`.
 *   PTRACE_GETREGS/SETREGS      `data` is a struct user_regs_struct*.
 *   PTRACE_CONT                 resume; if `data` is a signal, re-inject it.
 *   PTRACE_SINGLESTEP           resume with the trap flag (TF) set.
 *   PTRACE_DETACH               stop tracing pid and resume it.
 *   PTRACE_KILL                 terminate the tracee.
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

/*
 * Register layout for PTRACE_GETREGS / PTRACE_SETREGS.  Field order matches the
 * Linux/i386 user_regs_struct so a stock i386 debugger backend uses it
 * unchanged.
 */
struct user_regs_struct {
    unsigned int ebx, ecx, edx, esi, edi, ebp, eax;
    unsigned int xds, xes, xfs, xgs, orig_eax;
    unsigned int eip, xcs, eflags, esp, xss;
};

#endif /* _SYS_PTRACE_H */
