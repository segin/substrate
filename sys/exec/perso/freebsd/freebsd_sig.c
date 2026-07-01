#include <sys/signal.h>
#include <sys/proc.h>
#include <sys/copy.h>
#include <sys/errno.h>
#include <sys/kern_syscalls.h>
#include <exec/perso/freebsd/freebsd_user.h>
#include <arch/i386/idt.h>
#include <arch/i386/signal_arch.h>
#include <string.h>

/*
 * FreeBSD i386 signal delivery.
 *
 * FreeBSD (like every modern BSD) delivers signals with the SA_SIGINFO-shaped
 * argument list -- handler(int sig, siginfo_t *info, ucontext_t *uc) -- and a
 * real siginfo_t + ucontext_t built on the user stack.  libthr installs its
 * thr_sighandler wrapper with SA_SIGINFO unconditionally, so `info` must be a
 * valid pointer; passing 0 makes libthr's handle_signal() fault reading
 * info->si_addr (addr 0x18) when it re-dispatches a plain (non-SA_SIGINFO)
 * user handler such as SDL2's SIGINT/SIGTERM catcher.  That was the observed
 * PsyMP3 worker-thread SIGSEGV.
 *
 * The handler is entered directly (eip = handler); on return it lands in the
 * FreeBSD sigreturn trampoline (0xFE000030) which loads EBX = &sf_uc and
 * issues sigreturn (syscall 119 -> .sigreturn hook -> freebsd_sys_sigreturn).
 */
void freebsd_sendsig(void *handler, int sig, uint32_t mask, uint32_t flags, void *regs_ptr) {
    registers_t *regs = (registers_t *)regs_ptr;
    uint32_t esp = regs->useresp;

    /* Alternate signal stack (SA_ONSTACK), same policy as native sendsig. */
    if ((flags & SA_ONSTACK) &&
        (current_thread->sig_alt_stack.ss_flags & SS_DISABLE) == 0 &&
        !current_thread->sig_on_stack) {
        esp = (uint32_t)(uintptr_t)current_thread->sig_alt_stack.ss_sp +
              (uint32_t)current_thread->sig_alt_stack.ss_size;
        current_thread->sig_on_stack = 1;
        current_thread->sig_alt_stack.ss_flags |= SS_ONSTACK;
    }

    struct freebsd_rt_sigframe frame;
    memset(&frame, 0, sizeof(frame));

    esp -= sizeof(frame);
    esp &= ~0xFUL;

    if (validate_user_addr((void *)(uintptr_t)esp, sizeof(frame)) != 0) {
        sigexit(current_process, SIGSEGV);
        return;
    }

    uint32_t uc_user = esp + offsetof(struct freebsd_rt_sigframe, sf_uc);
    uint32_t si_user = esp + offsetof(struct freebsd_rt_sigframe, sf_si);

    /* Fault details only apply when we are delivering the very trap that just
     * happened on this thread; for an async signal (kill/pthread_kill) there
     * is no faulting address and si_code is SI_USER (0). */
    uint32_t fault_addr = 0;
    int32_t  si_code = 0;
    if (sig == current_thread->trap_signo) {
        fault_addr = current_thread->trap_addr;
        si_code = current_thread->trap_code;
    }

    frame.sf_ra       = FBSD_SIG_TRAMPOLINE_ADDR;
    frame.sf_signum   = sig;
    frame.sf_ucontext = uc_user;               /* arg3 is always a ucontext */
    frame.sf_addr     = fault_addr;            /* arg4 */
    frame.sf_ahu      = (uint32_t)(uintptr_t)handler;

    if (flags & SA_SIGINFO) {
        frame.sf_siginfo   = si_user;          /* arg2 -> &sf_si */
        frame.sf_si.si_signo = sig;
        frame.sf_si.si_errno = 0;
        frame.sf_si.si_code  = si_code;
        frame.sf_si.si_pid   = current_process ? current_process->pid : 0;
        frame.sf_si.si_uid   = current_process ? current_process->uid : 0;
        frame.sf_si.si_addr  = fault_addr;
    } else {
        /* Traditional (pre-SA_SIGINFO) handlers take the trap code as arg2. */
        frame.sf_siginfo = (uint32_t)si_code;
    }

    /* Machine state the handler may inspect and sigreturn will restore. */
    struct freebsd_mcontext *mc = &frame.sf_uc.uc_mcontext;
    mc->mc_onstack   = 0;
    mc->mc_gs        = regs->gs;
    mc->mc_fs        = regs->fs;
    mc->mc_es        = regs->es;
    mc->mc_ds        = regs->ds;
    mc->mc_edi       = regs->edi;
    mc->mc_esi       = regs->esi;
    mc->mc_ebp       = regs->ebp;
    mc->mc_isp       = 0;
    mc->mc_ebx       = regs->ebx;
    mc->mc_edx       = regs->edx;
    mc->mc_ecx       = regs->ecx;
    mc->mc_eax       = regs->eax;
    mc->mc_trapno    = regs->int_no;
    mc->mc_err       = regs->err_code;
    mc->mc_eip       = regs->eip;
    mc->mc_cs        = regs->cs;
    mc->mc_eflags    = regs->eflags;
    mc->mc_esp       = regs->useresp;
    mc->mc_ss        = regs->ss;
    mc->mc_len       = FREEBSD_MC_LEN;
    mc->mc_fpformat  = FREEBSD_MC_FPFMT_NODEV;  /* no FPU state marshalled */
    mc->mc_ownedfp   = FREEBSD_MC_FPOWNED_NONE;
    mc->mc_flags     = 0;

    /* Signal mask to restore on sigreturn (only the low 32 bits are used by
     * substrate's native signal machinery). */
    frame.sf_uc.uc_sigmask.__bits[0] = mask;

    frame.sf_uc.uc_stack_ss_sp    = (uint32_t)(uintptr_t)current_thread->sig_alt_stack.ss_sp;
    frame.sf_uc.uc_stack_ss_size  = (uint32_t)current_thread->sig_alt_stack.ss_size;
    frame.sf_uc.uc_stack_ss_flags = current_thread->sig_alt_stack.ss_flags;

    if (copyout(&frame, (void *)(uintptr_t)esp, sizeof(frame)) != 0) {
        sigexit(current_process, SIGSEGV);
        return;
    }

    regs->useresp = esp;
    regs->eip     = (uint32_t)(uintptr_t)handler;
    regs->eflags &= ~(1 << 10);   /* clear DF per the i386 calling convention */
}

int freebsd_sys_sigreturn(void *regs_ptr) {
    registers_t *regs = (registers_t *)regs_ptr;
    /* The trampoline loaded EBX with &sf_uc (the ucontext pointer). */
    struct freebsd_ucontext *uc_user = (struct freebsd_ucontext *)regs->ebx;

    struct freebsd_ucontext uc;
    if (copyin(uc_user, &uc, sizeof(uc)) != 0) return -EFAULT;

    struct freebsd_mcontext *mc = &uc.uc_mcontext;

    regs->eip     = mc->mc_eip;
    regs->eax     = mc->mc_eax;
    regs->ebx     = mc->mc_ebx;
    regs->ecx     = mc->mc_ecx;
    regs->edx     = mc->mc_edx;
    regs->edi     = mc->mc_edi;
    regs->esi     = mc->mc_esi;
    regs->ebp     = mc->mc_ebp;
    regs->useresp = mc->mc_esp;
    /* EFLAGS sanitize: keep kernel-owned bits (IOPL/VM/RF/NT), take only user
     * bits from the frame -- a raw assignment is an IOPL privilege escalation.
     * Mirrors native sys_sigreturn. */
    regs->eflags = (regs->eflags & 0x00033200u) | ((uint32_t)mc->mc_eflags & 0xFFCCCDFFu);
    regs->cs = mc->mc_cs | 3;
    regs->ss = mc->mc_ss | 3;
    regs->ds = mc->mc_ds | 3;
    regs->es = mc->mc_es | 3;
    regs->fs = mc->mc_fs | 3;
    regs->gs = mc->mc_gs | 3;

    current_thread->sig_mask = uc.uc_sigmask.__bits[0];
    current_thread->sig_on_stack = 0;
    current_thread->sig_alt_stack.ss_flags &= ~SS_ONSTACK;
    /* Trapframe is now the restored user context -- the dispatcher must not
     * apply its eax/edx/CF writebacks (see sys_sigreturn). */
    current_thread->frame_replaced = 1;
    return regs->eax;
}

/* FreeBSD sa_flags -> substrate-native sa_flags (different bit assignments). */
static uint32_t freebsd_sa_flags_to_native(int32_t f) {
    uint32_t n = 0;
    if (f & FBSD_SA_ONSTACK)   n |= SA_ONSTACK;
    if (f & FBSD_SA_RESTART)   n |= SA_RESTART;
    if (f & FBSD_SA_RESETHAND) n |= SA_RESETHAND;
    if (f & FBSD_SA_NOCLDSTOP) n |= SA_NOCLDSTOP;
    if (f & FBSD_SA_NODEFER)   n |= SA_NODEFER;
    if (f & FBSD_SA_NOCLDWAIT) n |= SA_NOCLDWAIT;
    if (f & FBSD_SA_SIGINFO)   n |= SA_SIGINFO;
    return n;
}

static int32_t native_sa_flags_to_freebsd(uint32_t n) {
    int32_t f = 0;
    if (n & SA_ONSTACK)   f |= FBSD_SA_ONSTACK;
    if (n & SA_RESTART)   f |= FBSD_SA_RESTART;
    if (n & SA_RESETHAND) f |= FBSD_SA_RESETHAND;
    if (n & SA_NOCLDSTOP) f |= FBSD_SA_NOCLDSTOP;
    if (n & SA_NODEFER)   f |= FBSD_SA_NODEFER;
    if (n & SA_NOCLDWAIT) f |= FBSD_SA_NOCLDWAIT;
    if (n & SA_SIGINFO)   f |= FBSD_SA_SIGINFO;
    return f;
}

/*
 * FreeBSD sigaction(2).
 *
 * FreeBSD's struct sigaction orders its members { handler; int sa_flags;
 * sigset_t sa_mask } and its sa_flags bits differ from substrate-native's
 * { handler; mask; flags }.  Routing FreeBSD's struct straight through the
 * native sys_sigaction misreads sa_flags (it lands on sa_mask) and the
 * SA_* bit values -- so SA_SIGINFO could never be recognised and the
 * during-handler mask was garbage.  Marshal both directions explicitly.
 *
 * Signal *numbers* are deliberately left untranslated: kill(2)/thr_kill(2)/
 * psignal deliver in FreeBSD's number space too, so install and delivery stay
 * self-consistent (translating only here would desynchronise them).
 */
int freebsd_sys_sigaction(int sig, const void *act, void *oact) {
    struct sigaction kact, koact;
    struct sigaction *pkact = NULL;
    struct freebsd_sigaction fact, foact;

    if (act) {
        if (copyin(act, &fact, sizeof(fact)) != 0) return -EFAULT;
        kact.sa_handler = (sig_t)(uintptr_t)fact.sa_handler;
        kact.sa_flags   = (int)freebsd_sa_flags_to_native(fact.sa_flags);
        kact.sa_mask    = fact.sa_mask.__bits[0];
        pkact = &kact;
    }

    int ret = kern_sigaction(sig, pkact, oact ? &koact : NULL);
    if (ret != 0) return ret;

    if (oact) {
        memset(&foact, 0, sizeof(foact));
        foact.sa_handler        = (uint32_t)(uintptr_t)koact.sa_handler;
        foact.sa_flags          = native_sa_flags_to_freebsd((uint32_t)koact.sa_flags);
        foact.sa_mask.__bits[0] = koact.sa_mask;
        if (copyout(&foact, oact, sizeof(foact)) != 0) return -EFAULT;
    }
    return 0;
}
