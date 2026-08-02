#ifndef _OPENBSD_USER_H
#define _OPENBSD_USER_H

#include <stdint.h>

/* OpenBSD i386 sigcontext.
 *
 * Field order matches OpenBSD's NetBSD-derived layout: sc_eip follows
 * sc_eax directly, and sc_trapno/sc_err sit at the END after sc_onstack/
 * sc_mask (see netbsd/sys/arch/i386/include/signal.h sc_gs..sc_err).  The
 * FreeBSD ABI interleaves sc_trapno/sc_err between sc_eax and sc_eip, which
 * is wrong here: it pushes sc_eip/sc_esp 8 bytes past where OpenBSD libc,
 * trampolines and unwinders expect them (sc_eip at offset 44, not 52).
 */
struct openbsd_sigcontext {
    int32_t sc_gs;
    int32_t sc_fs;
    int32_t sc_es;
    int32_t sc_ds;
    int32_t sc_edi;
    int32_t sc_esi;
    int32_t sc_ebp;
    int32_t sc_ebx;
    int32_t sc_edx;
    int32_t sc_ecx;
    int32_t sc_eax;
    int32_t sc_eip;
    int32_t sc_cs;
    int32_t sc_eflags;
    int32_t sc_esp;
    int32_t sc_ss;
    int32_t sc_onstack;
    uint32_t sc_mask;
    int32_t sc_trapno;
    int32_t sc_err;
};

/* OpenBSD i386 sigframe */
struct openbsd_sigframe {
    int32_t  sf_sig;
    int32_t  sf_code;
    uint32_t sf_scp;    /* struct sigcontext * */
    uint32_t sf_handler;
    struct openbsd_sigcontext sf_sc;
};

/* OpenBSD signal translation functions */
void openbsd_sendsig(void *handler, int sig, uint32_t mask, uint32_t flags, void *regs);
int  openbsd_sys_sigreturn(void *regs);

/* OpenBSD <-> native signal-number / mask translation (openbsd_sig.c).
 * OpenBSD uses the historical 4.3BSD signal numbering (SIGCHLD=20,
 * SIGUSR1=30, SIGUSR2=31, SIGBUS=10, SIGSTOP=17, SIGCONT=19, SIGSYS=12) --
 * identical to NetBSD -- whereas substrate-native is Linux-ish. */
int      openbsd_to_native_signo(int sig);
int      native_to_openbsd_signo(int sig);
uint32_t openbsd_to_native_sigmask(uint32_t m);
uint32_t native_to_openbsd_sigmask(uint32_t m);

/* OpenBSD signal syscall wrappers (translate numbers/masks/sa_flags).
 * sigprocmask/sigsuspend/sigpending use OpenBSD's register-based ABI: the
 * mask is passed by value and the old/pending mask is the return value. */
int openbsd_sys_sigaction(int sig, const void *nsa, void *osa);
int openbsd_sys_kill(int pid, int sig);
int openbsd_sys_sigprocmask(int how, uint32_t mask);
int openbsd_sys_sigsuspend(uint32_t mask);
int openbsd_sys_sigpending(void);

#endif
