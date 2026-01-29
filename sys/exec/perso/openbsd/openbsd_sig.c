#include <sys/signal.h>
#include <sys/proc.h>
#include <exec/perso/openbsd/openbsd_user.h>
#include <arch/i386/idt.h>
#include <string.h>

extern int copyout(const void *src, void *dst, size_t size);
extern int copyin(const void *src, void *dst, size_t size);
extern int validate_user_addr(const void *addr, size_t size);

void openbsd_sendsig(void *handler, int sig, uint32_t mask, uint32_t flags, void *regs_ptr) {
    (void)flags;
    registers_t *regs = (registers_t *)regs_ptr;
    uint32_t esp = regs->useresp;
    
    struct openbsd_sigframe frame;
    memset(&frame, 0, sizeof(frame));

    esp -= sizeof(struct openbsd_sigframe);
    esp &= ~0xFUL;

    if (validate_user_addr((void*)(uintptr_t)esp, sizeof(frame)) != 0) {
        sigexit(current_process, SIGSEGV);
        return;
    }

    frame.sf_sig = sig;
    frame.sf_code = 0;
    frame.sf_scp = esp + offsetof(struct openbsd_sigframe, sf_sc);
    frame.sf_handler = (uint32_t)handler;

    frame.sf_sc.sc_eip = regs->eip;
    frame.sf_sc.sc_eax = regs->eax;
    frame.sf_sc.sc_ebx = regs->ebx;
    frame.sf_sc.sc_ecx = regs->ecx;
    frame.sf_sc.sc_edx = regs->edx;
    frame.sf_sc.sc_edi = regs->edi;
    frame.sf_sc.sc_esi = regs->esi;
    frame.sf_sc.sc_ebp = regs->ebp;
    frame.sf_sc.sc_esp = regs->useresp;
    frame.sf_sc.sc_eflags = regs->eflags;
    frame.sf_sc.sc_cs = regs->cs;
    frame.sf_sc.sc_ss = regs->ss;
    frame.sf_sc.sc_ds = regs->ds;
    frame.sf_sc.sc_es = regs->es;
    frame.sf_sc.sc_fs = regs->fs;
    frame.sf_sc.sc_gs = regs->gs;
    frame.sf_sc.sc_mask = mask;

    if (copyout(&frame, (void*)(uintptr_t)esp, sizeof(frame)) != 0) {
        sigexit(current_process, SIGSEGV);
        return;
    }

    regs->useresp = esp;
    regs->eip = (uint32_t)handler;
}

int openbsd_sys_sigreturn(void *regs_ptr) {
    registers_t *regs = (registers_t *)regs_ptr;
    struct openbsd_sigcontext *scp_user = (struct openbsd_sigcontext *)regs->ebx;
    
    struct openbsd_sigcontext sc;
    if (copyin(scp_user, &sc, sizeof(sc)) != 0) return -1;

    regs->eip = sc.sc_eip;
    regs->eax = sc.sc_eax;
    regs->ebx = sc.sc_ebx;
    regs->ecx = sc.sc_ecx;
    regs->edx = sc.sc_edx;
    regs->edi = sc.sc_edi;
    regs->esi = sc.sc_esi;
    regs->ebp = sc.sc_ebp;
    regs->useresp = sc.sc_esp;
    regs->eflags = sc.sc_eflags;
    regs->cs = sc.sc_cs | 3;
    regs->ss = sc.sc_ss | 3;
    regs->ds = sc.sc_ds | 3;
    regs->es = sc.sc_es | 3;
    regs->fs = sc.sc_fs | 3;
    regs->gs = sc.sc_gs | 3;

    current_thread->sig_mask = sc.sc_mask;
    return regs->eax;
}
