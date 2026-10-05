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
 *                               through `data` (a uint32_t*, or a uint64_t*
 *                               when the tracer is 64-bit).  Returns 0/-errno
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
#define PTRACE_GETFPREGS    14
#define PTRACE_SETFPREGS    15
#define PTRACE_ATTACH       16
#define PTRACE_DETACH       17
#define PTRACE_GETFPXREGS   18
#define PTRACE_SETFPXREGS   19
#define PTRACE_GETXSTATE_INFO 0x4300
#define PTRACE_GETXSTATE    0x4301
#define PTRACE_SETXSTATE    0x4302

/*
 * Floating-point and vector registers, in the CPU's own save layouts,
 * which do not depend on the tracer's width.  A layout the CPU does not
 * save in is EIO; a debugger tries them from the richest down.
 *
 *   PTRACE_GETXSTATE/SETXSTATE    the XSAVE area (x87, SSE, AVX, ...):
 *                                 `data` is the buffer, `addr` its length,
 *                                 which must be xsave_len.
 *   PTRACE_GETXSTATE_INFO         `data` is a struct ptrace_xstate_info*.
 *   PTRACE_GETFPXREGS/SETFPXREGS  the FXSAVE image (x87 and SSE): `data`
 *                                 is a PTRACE_FPXREGS_SIZE-byte buffer.
 *   PTRACE_GETFPREGS/SETFPREGS    the FNSAVE image (x87): `data` is a
 *                                 PTRACE_FPREGS_SIZE-byte buffer.  Only on
 *                                 a CPU without FXSAVE.
 */
#define PTRACE_FPREGS_SIZE  108
#define PTRACE_FPXREGS_SIZE 512

struct ptrace_xstate_info {
    unsigned long long xsave_mask;  /* the components saved: XCR0 */
    unsigned int       xsave_len;   /* size of the XSAVE area */
    unsigned int       pad;
};

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

/*
 * The register set a 64-bit tracer exchanges with PTRACE_GETREGS / SETREGS:
 * the Linux/amd64 user_regs_struct, field for field, so gdb's stock amd64
 * register offsets apply.  A 64-bit tracer gets this layout whatever its
 * tracee is; a 32-bit tracee's registers come back zero-extended.  Userland
 * sees it as struct user_regs_struct when compiled for x86-64
 * (include/sys/ptrace.h).  A 64-bit tracer's PEEK and POKE move 8-byte
 * words, its sizeof(long).
 */
struct user_regs_struct64 {
    unsigned long long r15, r14, r13, r12, rbp, rbx, r11, r10;
    unsigned long long r9, r8, rax, rcx, rdx, rsi, rdi, orig_rax;
    unsigned long long rip, cs, eflags, rsp, ss;
    unsigned long long fs_base, gs_base;
    unsigned long long ds, es, fs, gs;
};

#endif /* _SYS_PTRACE_H */
