#include <string.h>

#include <arch/i386/idt.h>
#include <arch/i386/signal_arch.h>
#include <exec/perso/freebsd/freebsd_user.h>
#include <sys/copy.h>
#include <sys/errno.h>
#include <sys/kern_syscalls.h>
#include <sys/proc.h>
#include <sys/signal.h>
#include <sys/syscall_impl.h>

/*
 * FreeBSD <-> substrate-native signal-number translation.
 *
 * FreeBSD i386 uses the historical 4.3BSD signal numbering (SIGCHLD=20,
 * SIGUSR1=30, SIGUSR2=31, SIGBUS=10, SIGSYS=12, SIGSTOP=17, SIGTSTP=18,
 * SIGCONT=19 -- verified against freebsd sys/sys/signal.h), which is the
 * SAME numbering NetBSD uses; substrate's native numbering is Linux-ish
 * (SIGCHLD=17, SIGUSR1=10, SIGBUS=7, where 20=SIGTSTP).  The shared kernel
 * signal machinery -- pending sets, blocked masks, psignal(), the SIGCHLD
 * raised on child exit, the SIGTSTP a tty raises on Ctrl-Z -- all speak
 * NATIVE numbers, so every FreeBSD signal syscall argument (and the
 * number/mask handed to a signal handler at delivery) must be translated at
 * the personality boundary.  Without this a FreeBSD program's
 * sigaction(SIGCHLD=20) installs on the native SIGTSTP slot, its kernel
 * SIGCHLD is never caught, and a Ctrl-Z spuriously fires its handler.
 *
 * These tables carry the same values as the NetBSD personality's
 * (netbsd_sig.c); the numberings are identical.  Indexed by signal number
 * (1..31); 0 and out-of-range fall back to identity.
 */
static const unsigned char fbsd2nat_signo[32] = {
    [1] = SIGHUP,  [2] = SIGINT,  [3] = SIGQUIT, [4] = SIGILL,
    [5] = SIGTRAP, [6] = SIGABRT,
    [7] = SIGBUS,                 /* FreeBSD SIGEMT -> nearest native trap */
    [8] = SIGFPE,  [9] = SIGKILL,
    [10] = SIGBUS,                /* FreeBSD SIGBUS=10  */
    [11] = SIGSEGV,
    [12] = SIGSYS,                /* FreeBSD SIGSYS=12 -> native 31 */
    [13] = SIGPIPE, [14] = SIGALRM, [15] = SIGTERM, [16] = SIGURG,
    [17] = SIGSTOP,               /* FreeBSD SIGSTOP=17 -> native 19 */
    [18] = SIGTSTP,               /* FreeBSD SIGTSTP=18 -> native 20 */
    [19] = SIGCONT,               /* FreeBSD SIGCONT=19 -> native 18 */
    [20] = SIGCHLD,               /* FreeBSD SIGCHLD=20 -> native 17 */
    [21] = SIGTTIN, [22] = SIGTTOU,
    [23] = SIGIO,                 /* FreeBSD SIGIO=23 */
    [24] = 24, [25] = 25,         /* XCPU / XFSZ -> unused native slots */
    [26] = SIGVTALRM, [27] = SIGPROF, [28] = SIGWINCH,
    [29] = 29,                    /* SIGINFO -> unused native slot */
    [30] = SIGUSR1,               /* FreeBSD SIGUSR1=30 -> native 10 */
    [31] = SIGUSR2,               /* FreeBSD SIGUSR2=31 -> native 12 */
};

static const unsigned char nat2fbsd_signo[32] = {
    [SIGHUP] = 1,  [SIGINT] = 2,  [SIGQUIT] = 3, [SIGILL] = 4,
    [SIGTRAP] = 5, [SIGABRT] = 6,
    [SIGBUS] = 10,                /* native SIGBUS=7 -> FreeBSD 10 */
    [SIGFPE] = 8,  [SIGKILL] = 9,
    [SIGUSR1] = 30,               /* native SIGUSR1=10 -> FreeBSD 30 */
    [SIGSEGV] = 11,
    [SIGUSR2] = 31,               /* native SIGUSR2=12 -> FreeBSD 31 */
    [SIGPIPE] = 13, [SIGALRM] = 14, [SIGTERM] = 15, [SIGURG] = 16,
    [SIGCHLD] = 20,               /* native SIGCHLD=17 -> FreeBSD 20 */
    [SIGCONT] = 19,               /* native SIGCONT=18 -> FreeBSD 19 */
    [SIGSTOP] = 17,               /* native SIGSTOP=19 -> FreeBSD 17 */
    [SIGTSTP] = 18,               /* native SIGTSTP=20 -> FreeBSD 18 */
    [SIGTTIN] = 21, [SIGTTOU] = 22,
    [SIGIO] = 23,
    [24] = 24, [25] = 25,
    [SIGVTALRM] = 26, [SIGPROF] = 27, [SIGWINCH] = 28,
    [29] = 29, [30] = 30,
    [SIGSYS] = 12,                /* native SIGSYS=31 -> FreeBSD 12 */
};

int freebsd_to_native_signo(int sig) {
    if (sig <= 0 || sig >= 32) return sig;
    int n = fbsd2nat_signo[sig];
    return n ? n : sig;
}

int native_to_freebsd_signo(int sig) {
    if (sig <= 0 || sig >= 32) return sig;
    int n = nat2fbsd_signo[sig];
    return n ? n : sig;
}

/* Remap a signal bitmask (bit (signo-1) set) between the two numberings.
 * Only signals 1..31 are meaningful in substrate's 32-bit mask word. */
uint32_t freebsd_to_native_sigmask(uint32_t m) {
    uint32_t out = 0;
    for (int s = 1; s <= 31; s++)
        if (m & (1u << (s - 1)))
            out |= 1u << (freebsd_to_native_signo(s) - 1);
    return out;
}

uint32_t native_to_freebsd_sigmask(uint32_t m) {
    uint32_t out = 0;
    for (int s = 1; s <= 31; s++)
        if (m & (1u << (s - 1)))
            out |= 1u << (native_to_freebsd_signo(s) - 1);
    return out;
}

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

    /* Alternate signal stack (SA_ONSTACK), same policy as native sendsig.
     * Use it ONLY if one is actually installed: a pthread thread has a
     * zero-initialised sig_alt_stack (ss_sp==NULL, ss_size==0, ss_flags==0, so
     * SS_DISABLE is clear), and checking SS_DISABLE alone would build the frame
     * at NULL+0 -> validate_user_addr fails -> the thread is killed with
     * SIGSEGV instead of the handler running. [EXEC-03] */
    if ((flags & SA_ONSTACK) &&
        current_thread->sig_alt_stack.ss_sp != NULL &&
        current_thread->sig_alt_stack.ss_size > 0 &&
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

    /* The kernel posts NATIVE signal numbers; the handler must see the
     * FreeBSD number it installed against. */
    int fsig = native_to_freebsd_signo(sig);

    frame.sf_ra       = FBSD_SIG_TRAMPOLINE_ADDR;
    frame.sf_signum   = fsig;
    frame.sf_ucontext = uc_user;               /* arg3 is always a ucontext */
    frame.sf_addr     = fault_addr;            /* arg4 */
    frame.sf_ahu      = (uint32_t)(uintptr_t)handler;

    if (flags & SA_SIGINFO) {
        frame.sf_siginfo   = si_user;          /* arg2 -> &sf_si */
        frame.sf_si.si_signo = fsig;
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
     * substrate's native signal machinery).  The user sees a FreeBSD-numbered
     * set; sigreturn translates it back to native. */
    frame.sf_uc.uc_sigmask.__bits[0] = native_to_freebsd_sigmask(mask);

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

    /* uc_sigmask is a FreeBSD-numbered set; the kernel mask is native. */
    current_thread->sig_mask = freebsd_to_native_sigmask(uc.uc_sigmask.__bits[0]);
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
 * The signal *number* and sa_mask are FreeBSD-numbered and must be
 * translated to native: the kernel indexes handlers and masks by native
 * number, and posts native numbers (delivery translates back in sendsig),
 * so installing on the raw FreeBSD number would land on the wrong slot.
 */
int freebsd_sys_sigaction(int sig, const void *act, void *oact) {
    struct sigaction kact, koact;
    struct sigaction *pkact = NULL;
    struct freebsd_sigaction fact, foact;

    int nat = freebsd_to_native_signo(sig);

    if (act) {
        if (copyin(act, &fact, sizeof(fact)) != 0) return -EFAULT;
        kact.sa_handler = (sig_t)(uintptr_t)fact.sa_handler;
        kact.sa_flags   = (int)freebsd_sa_flags_to_native(fact.sa_flags);
        kact.sa_mask    = freebsd_to_native_sigmask(fact.sa_mask.__bits[0]);
        pkact = &kact;
    }

    int ret = kern_sigaction(nat, pkact, oact ? &koact : NULL);
    if (ret != 0) return ret;

    if (oact) {
        memset(&foact, 0, sizeof(foact));
        foact.sa_handler        = (uint32_t)(uintptr_t)koact.sa_handler;
        foact.sa_flags          = native_sa_flags_to_freebsd((uint32_t)koact.sa_flags);
        foact.sa_mask.__bits[0] = native_to_freebsd_sigmask(koact.sa_mask);
        if (copyout(&foact, oact, sizeof(foact)) != 0) return -EFAULT;
    }
    return 0;
}

/* kill(2): translate the FreeBSD signal number to native. */
int freebsd_sys_kill(int pid, int sig) {
    return sys_kill(pid, freebsd_to_native_signo(sig));
}

/* thr_kill(2): the signo is FreeBSD-numbered (libthr pthread_kill). */
int freebsd_sys_thr_kill(long tid, int sig) {
    return sys_thr_kill(tid, freebsd_to_native_signo(sig));
}

/*
 * sigprocmask(2): the set/oset are FreeBSD-numbered sigset_t (only the first
 * 32-bit word is meaningful here).  Translate the incoming mask to native
 * bit positions, and the outgoing old mask back.
 */
int freebsd_sys_sigprocmask(int how, const void *set, void *oset) {
    uint32_t kset, koset;
    uint32_t *p_set = NULL;

    if (set) {
        uint32_t fset;
        if (copyin(set, &fset, sizeof(fset)) != 0) return -EFAULT;
        kset = freebsd_to_native_sigmask(fset);
        p_set = &kset;
    }

    int ret = kern_sigprocmask(how, p_set, oset ? &koset : NULL);
    if (ret != 0) return ret;

    if (oset) {
        uint32_t fold = native_to_freebsd_sigmask(koset);
        if (copyout(&fold, oset, sizeof(fold)) != 0) return -EFAULT;
    }
    return 0;
}

/* sigsuspend(2): the mask is FreeBSD-numbered. */
int freebsd_sys_sigsuspend(const void *mask) {
    uint32_t fmask = 0;
    if (mask && copyin(mask, &fmask, sizeof(fmask)) != 0) return -EFAULT;
    uint32_t kmask = freebsd_to_native_sigmask(fmask);
    return kern_sigsuspend(&kmask);
}

/* sigpending(2): the returned set is FreeBSD-numbered. */
int freebsd_sys_sigpending(void *set) {
    uint32_t kset = 0;
    int ret = kern_sigpending(&kset);
    if (ret != 0) return ret;
    if (set) {
        uint32_t fset = native_to_freebsd_sigmask(kset);
        if (copyout(&fset, set, sizeof(fset)) != 0) return -EFAULT;
    }
    return 0;
}
