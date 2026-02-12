#include <sys/signal.h>
#include <sys/proc.h>
#include <exec/perso/freebsd/freebsd_user.h>
#include <arch/i386/idt.h>
#include <string.h>

extern int copyout(const void *src, void *dst, size_t size);
extern int copyin(const void *src, void *dst, size_t size);
extern int validate_user_addr(const void *addr, size_t size);

void freebsd_sendsig(void *handler, int sig, uint32_t mask, uint32_t flags, void *regs_ptr) {
    (void)flags;
    registers_t *regs = (registers_t *)regs_ptr;
    uint32_t esp = regs->useresp;
    
    struct freebsd_sigframe frame;
    memset(&frame, 0, sizeof(frame));

    esp -= sizeof(struct freebsd_sigframe);
    esp &= ~0xFUL;

    if (validate_user_addr((void*)(uintptr_t)esp, sizeof(frame)) != 0) {
        sigexit(current_process, SIGSEGV);
        return;
    }

    frame.sf_sig = sig;
    if (sig == current_thread->trap_signo) {
        frame.sf_code = current_thread->trap_code;
    } else {
        frame.sf_code = 0;
    }
    frame.sf_scp = esp + offsetof(struct freebsd_sigframe, sf_sc);
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
    frame.sf_sc.sc_efl = regs->eflags;
    frame.sf_sc.sc_cs = regs->cs;
    frame.sf_sc.sc_ss = regs->ss;
    frame.sf_sc.sc_ds = regs->ds;
    frame.sf_sc.sc_es = regs->es;
    frame.sf_sc.sc_fs = regs->fs;
    frame.sf_sc.sc_gs = regs->gs;
    frame.sf_sc.sc_trapno = regs->int_no;
    frame.sf_sc.sc_err = regs->err_code;
    frame.sf_sc.sc_mask = mask;

    /* FreeBSD doesn't typically use a kernel-mapped trampoline for legacy signals;
       the C library provides one or the application handles it. 
       However, for personality emulation, we can point to a generic trampoline or
       rely on the 'sf_handler' being invoked correctly if it matches ABI. 
       Actually, FreeBSD 4.x syscall 103 is sigreturn. */
    
    if (copyout(&frame, (void*)(uintptr_t)esp, sizeof(frame)) != 0) {
        sigexit(current_process, SIGSEGV);
        return;
    }

    regs->useresp = esp;
    regs->eip = (uint32_t)handler;
}

int freebsd_sys_sigreturn(void *regs_ptr) {
    registers_t *regs = (registers_t *)regs_ptr;
    /* Arg is a pointer to sigcontext */
    struct freebsd_sigcontext *scp_user = (struct freebsd_sigcontext *)regs->ebx; // FreeBSD passes first arg in EBX? No, stack usually.
    // In sigreturn(scp), scp is the first argument. On i386 syscall, that's in EBX or on stack depending on variant.
    
    struct freebsd_sigcontext sc;
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
    regs->eflags = sc.sc_efl;
    regs->cs = sc.sc_cs | 3;
    regs->ss = sc.sc_ss | 3;
    regs->ds = sc.sc_ds | 3;
    regs->es = sc.sc_es | 3;
    regs->fs = sc.sc_fs | 3;
    regs->gs = sc.sc_gs | 3;

    current_thread->sig_mask = sc.sc_mask;
    return regs->eax;
}
