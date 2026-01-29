#ifndef _NETBSD_USER_H
#define _NETBSD_USER_H

#include <stdint.h>

/* NetBSD i386 sigcontext */
struct netbsd_sigcontext {
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

/* NetBSD i386 sigframe */
struct netbsd_sigframe {
    int32_t  sf_sig;
    int32_t  sf_code;
    uint32_t sf_scp;    /* struct sigcontext * */
    uint32_t sf_handler;
    struct netbsd_sigcontext sf_sc;
};

/* NetBSD signal translation functions */
void netbsd_sendsig(void *handler, int sig, uint32_t mask, uint32_t flags, void *regs);
int  netbsd_sys_sigreturn(void *regs);

#endif
