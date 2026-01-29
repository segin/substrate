#ifndef _OPENBSD_USER_H
#define _OPENBSD_USER_H

#include <stdint.h>

/* OpenBSD i386 sigcontext */
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
    int32_t sc_trapno;
    int32_t sc_err;
    int32_t sc_eip;
    int32_t sc_cs;
    int32_t sc_eflags;
    int32_t sc_esp;
    int32_t sc_ss;
    int32_t sc_onstack;
    uint32_t sc_mask;
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

#endif
