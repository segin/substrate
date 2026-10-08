/*
 * sunos_sig.c - signals as SunOS 4.0 on the Sun386i has them: sigvec(2),
 * the mask calls of 4.3BSD, and delivery.
 *
 * The signals are numbered as 4.3BSD numbered them, which is how FreeBSD
 * still does, so the numbers and masks are translated by its functions.
 * (SunOS's 29 is SIGLOST and FreeBSD's SIGINFO; nothing sends either.)
 */

#include <stddef.h>
#include <string.h>

#include <exec/perso/freebsd/freebsd_user.h>
#include <exec/perso/sunos/sunos_user.h>
#include <kern/sched.h>
#include <machine/idt.h>
#include <sys/copy.h>
#include <sys/errno.h>
#include <sys/kern_syscalls.h>
#include <sys/proc.h>
#include <sys/signal.h>

/*
 * sigvec(2).  "If the SV_INTERRUPT bit is not set, a system call
 * interrupted by the signal is restarted"; SV_ONSTACK takes the signal on
 * the stack sigstack(2) named; SV_RESETHAND puts the default back as the
 * signal is taken.
 */
int sunos_sys_sigvec(int sig, const struct sunos_sigvec *vec,
                     struct sunos_sigvec *ovec) {
    struct sigaction act, oact;
    struct sunos_sigvec v;
    int native = freebsd_to_native_signo(sig);
    int rc;

    if (sig <= 0 || sig >= 32) {
        return -EINVAL;
    }
    if (vec) {
        if (copyin(vec, &v, sizeof(v)) != 0) {
            return -EFAULT;
        }
        memset(&act, 0, sizeof(act));
        act.sa_handler = (sig_t)(uintptr_t)v.sv_handler;
        act.sa_mask = freebsd_to_native_sigmask((uint32_t)v.sv_mask);
        if (v.sv_flags & SUNOS_SV_ONSTACK)      act.sa_flags |= SA_ONSTACK;
        if (!(v.sv_flags & SUNOS_SV_INTERRUPT)) act.sa_flags |= SA_RESTART;
        if (v.sv_flags & SUNOS_SV_RESETHAND)    act.sa_flags |= SA_RESETHAND;
    }
    rc = kern_sigaction(native, vec ? &act : NULL, ovec ? &oact : NULL);
    if (rc != 0) {
        return rc;
    }
    if (ovec) {
        memset(&v, 0, sizeof(v));
        v.sv_handler = (uint32_t)(uintptr_t)oact.sa_handler;
        v.sv_mask = (int32_t)native_to_freebsd_sigmask(oact.sa_mask);
        if (oact.sa_flags & SA_ONSTACK)    v.sv_flags |= SUNOS_SV_ONSTACK;
        if (!(oact.sa_flags & SA_RESTART)) v.sv_flags |= SUNOS_SV_INTERRUPT;
        if (oact.sa_flags & SA_RESETHAND)  v.sv_flags |= SUNOS_SV_RESETHAND;
        if (copyout(&v, ovec, sizeof(v)) != 0) {
            return -EFAULT;
        }
    }
    return 0;
}

/* sigblock(2), sigsetmask(2): "the previous set of masked signals is
 * returned". */
static int change_mask(int how, int mask) {
    uint32_t set = freebsd_to_native_sigmask((uint32_t)mask);
    uint32_t old = 0;
    int rc = kern_sigprocmask(how, &set, &old);

    return rc != 0 ? rc : (int)native_to_freebsd_sigmask(old);
}

int sunos_sys_sigblock(int mask) {
    return change_mask(SIG_BLOCK, mask);
}

int sunos_sys_sigsetmask(int mask) {
    return change_mask(SIG_SETMASK, mask);
}

/* sigpause(2): wait for a signal with this mask, then put the old back. */
int sunos_sys_sigpause(int mask) {
    uint32_t set = freebsd_to_native_sigmask((uint32_t)mask);

    return kern_sigsuspend(&set);
}

/*
 * sigstack(2).  It names the top of a stack and nothing more -- "there is
 * no way to specify the size" -- so the thread's alternate stack is kept
 * as that address with no size, which only sunos_sendsig reads.
 */
int sunos_sys_sigstack(const struct sunos_sigstack *ss,
                       struct sunos_sigstack *oss) {
    struct sunos_sigstack k;

    if (!current_thread) {
        return -EINVAL;
    }
    if (oss) {
        k.ss_sp = (uint32_t)(uintptr_t)current_thread->sig_alt_stack.ss_sp;
        k.ss_onstack = current_thread->sig_on_stack ? 1 : 0;
        if (copyout(&k, oss, sizeof(k)) != 0) {
            return -EFAULT;
        }
    }
    if (ss) {
        if (copyin(ss, &k, sizeof(k)) != 0) {
            return -EFAULT;
        }
        current_thread->sig_alt_stack.ss_sp = (void *)(uintptr_t)k.ss_sp;
        current_thread->sig_alt_stack.ss_size = 0;
        current_thread->sig_alt_stack.ss_flags = 0;
        current_thread->sig_on_stack = k.ss_onstack ? 1 : 0;
    }
    return 0;
}

/*
 * Deliver a signal.  sigvec(2): "void handler(sig, code, scp, addr)".
 * The handler the kernel knows is libc's _sigtramp, which sigvec()
 * installs in place of the program's; it is entered with those four on
 * the stack and no return address, and comes back through sigcleanup.
 */
void sunos_sendsig(void *handler, int sig, uint32_t mask, uint32_t flags,
                   void *regs_ptr) {
    registers_t *regs = (registers_t *)regs_ptr;
    struct sunos_sigframe frame;
    uint32_t esp = regs->useresp;
    int was_on_stack = current_thread->sig_on_stack ? 1 : 0;

    if ((flags & SA_ONSTACK) && !was_on_stack &&
        current_thread->sig_alt_stack.ss_sp != NULL) {
        esp = (uint32_t)(uintptr_t)current_thread->sig_alt_stack.ss_sp;
        current_thread->sig_on_stack = 1;
    }
    esp -= sizeof(frame);
    esp &= ~3U;
    if (validate_user_addr((void *)(uintptr_t)esp, sizeof(frame)) != 0) {
        sigexit(current_process, SIGSEGV);
        return;
    }

    memset(&frame, 0, sizeof(frame));
    frame.sf_signum = native_to_freebsd_signo(sig);
    frame.sf_scp = esp + (uint32_t)offsetof(struct sunos_sigframe, sf_sc);
    if (sig == current_thread->trap_signo) {
        frame.sf_code = current_thread->trap_code;
        frame.sf_addr = current_thread->trap_addr;
    }
    frame.sf_sc.sc_onstack = was_on_stack;
    frame.sf_sc.sc_mask = (int32_t)native_to_freebsd_sigmask(mask);
    frame.sf_sc.sc_sp = regs->useresp;
    frame.sf_sc.sc_pc = regs->eip;
    frame.sf_sc.sc_ps = regs->eflags;
    frame.sf_sc.sc_eax = regs->eax;
    frame.sf_sc.sc_edx = regs->edx;
    if (copyout(&frame, (void *)(uintptr_t)esp, sizeof(frame)) != 0) {
        sigexit(current_process, SIGSEGV);
        return;
    }
    regs->useresp = esp;
    regs->eip = (uint32_t)(uintptr_t)handler;
    regs->eflags &= ~(1U << 10);        /* the direction flag, clear */
}

/*
 * sigcleanup: the way back from a handler, issued by _sigtramp alone.  It
 * takes no arguments as a call does; the stack pointer is at the address
 * of the sigcontext the handler was given.
 */
int sunos_sys_sigcleanup(void) {
    registers_t *regs = current_thread
        ? (registers_t *)current_thread->syscall_regs : NULL;
    struct sunos_sigcontext sc;
    uint32_t scp, set;

    if (!regs) {
        return -EINVAL;
    }
    if (copyin((const void *)(uintptr_t)regs->useresp, &scp,
               sizeof(scp)) != 0 ||
        copyin((const void *)(uintptr_t)scp, &sc, sizeof(sc)) != 0) {
        sigexit(current_process, SIGSEGV);
        return -EFAULT;
    }
    set = freebsd_to_native_sigmask((uint32_t)sc.sc_mask);
    kern_sigprocmask(SIG_SETMASK, &set, NULL);
    current_thread->sig_on_stack = sc.sc_onstack ? 1 : 0;

    regs->useresp = sc.sc_sp;
    regs->eip = sc.sc_pc;
    regs->eflags = (regs->eflags & ~SUNOS_PS_USER) |
                   (sc.sc_ps & SUNOS_PS_USER);
    regs->eax = sc.sc_eax;
    regs->edx = sc.sc_edx;
    current_thread->frame_replaced = 1;
    return 0;
}
