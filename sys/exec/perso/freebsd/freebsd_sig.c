#include <sys/signal.h>
#include <sys/proc.h>
#include <sys/copy.h>
#include <exec/perso/freebsd/freebsd_user.h>
#include <arch/i386/idt.h>
#include <string.h>

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

    /*
     * FreeBSD supplies its own signal-return trampoline from the C library
     * (the handler returns into it and it issues sigreturn -- syscall 103
     * on the FreeBSD 4.x ABI), so the kernel does not install one here.
     */
    if (copyout(&frame, (void*)(uintptr_t)esp, sizeof(frame)) != 0) {
        sigexit(current_process, SIGSEGV);
        return;
    }

    regs->useresp = esp;
    regs->eip = (uint32_t)handler;
}

int freebsd_sys_sigreturn(void *regs_ptr) {
    registers_t *regs = (registers_t *)regs_ptr;
    /* sigreturn(scp): the sigcontext pointer is the sole argument, passed
     * in EBX by the trampoline that issues the syscall. */
    struct freebsd_sigcontext *scp_user = (struct freebsd_sigcontext *)regs->ebx;

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
    /* EFLAGS sanitize: keep kernel-owned bits (IOPL/VM/RF/NT), take only
     * user bits from the frame — a raw assignment is an IOPL privilege
     * escalation.  Mirrors native sys_sigreturn. */
    regs->eflags = (regs->eflags & 0x00033200u) | (sc.sc_efl & 0xFFCCCDFFu);
    regs->cs = sc.sc_cs | 3;
    regs->ss = sc.sc_ss | 3;
    regs->ds = sc.sc_ds | 3;
    regs->es = sc.sc_es | 3;
    regs->fs = sc.sc_fs | 3;
    regs->gs = sc.sc_gs | 3;

    current_thread->sig_mask = sc.sc_mask;
    /* Trapframe is now the restored user context — the dispatcher must
     * not apply its eax/edx/CF writebacks (see sys_sigreturn). */
    current_thread->frame_replaced = 1;
    return regs->eax;
}
