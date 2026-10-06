/*
 * sysv386.h - the System V/386 system call entry, shared by the
 * personalities that use it.
 *
 * Xenix/386 and UNIX System V Release 4 for the 386 enter the kernel the
 * same way: `lcall $7,$0` with the call number in EAX and the arguments on
 * the stack above the stub's return address, carry and an errno in EAX on
 * failure, a second result in EDX, and `lcall $0xf,$0` to return from a
 * signal handler through a trampoline libc supplies.  The call numbers
 * below 64 are the same too.  perso_xenix.c implements the entry and the
 * classic calls; perso_svr4.c adds what Release 4 added.
 */
#ifndef _EXEC_PERSO_XENIX_SYSV386_H
#define _EXEC_PERSO_XENIX_SYSV386_H

#include <stdint.h>

#include <machine/idt.h>

/* A decoded call.  nr is AL and sub is AH (the sub-function of a
 * multiplexed call); a[] are the arguments. */
struct x386_frame {
    registers_t *regs;
    uint32_t nr;
    uint32_t sub;
    uint32_t a[6];
};

/*
 * A personality's call table: run the call in `f` and return its result
 * (negative errno, or the value for EAX with EDX's in the high half), or
 * set *known to 0 if it has no such call.
 */
typedef int64_t (*sysv386_callfn)(struct x386_frame *f, int *known);

/* What the faulting instruction at CS:EIP is. */
#define SYSV386_NOT_LCALL   0
#define SYSV386_SYSCALL     1   /* lcall $7,$0 */
#define SYSV386_SIGRETURN   2   /* lcall $0xf,$0 */
int sysv386_lcall_kind(registers_t *regs);

/* Emulate the `lcall $7,$0` at CS:EIP through `call`; `tag` prefixes the
 * trace lines, which `trace` turns on.  Returns 1 (the trap is handled). */
int sysv386_syscall(registers_t *regs, sysv386_callfn call, const char *tag,
                    int trace);

/* The Xenix/386 table, which is also System V's for calls 1 to 63. */
int64_t xenix386_call(struct x386_frame *f, int *known);

/*
 * Enter `handler` for a signal, with `tramp` as its return address and
 * `nargs` argument words above that: `signo`, then zeroes.  Below the
 * arguments goes the context sysv386_sigreturn() restores -- the
 * trampoline pops the signal number and makes the call, so `skip` there is
 * the number of argument words still on the stack, nargs - 1.
 */
void sysv386_sendsig(void *handler, uint32_t signo, uint32_t mask,
                     registers_t *regs, uint32_t tramp, unsigned int nargs);
int sysv386_sigreturn(registers_t *regs, unsigned int skip);

/* [addr, addr+len) lies below the top of user space: 0, or -EFAULT. */
int x386_span(uint32_t addr, uint32_t len);
/* A kernel copy of the string at `addr`; free with x386_free_string(). */
int x386_string(uint32_t addr, char **out);
void x386_free_string(char *s);
/* Two results: `first` for EAX, `second` for EDX. */
int64_t x386_pair(uint32_t first, uint32_t second);

#endif /* _EXEC_PERSO_XENIX_SYSV386_H */
