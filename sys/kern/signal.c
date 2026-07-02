#include <sys/signal.h>
#include <sys/kern_syscalls.h>
#include <sys/proc.h>
#include <sys/session.h>
#include <sys/errno.h>
#include <kern/sched.h>
#include <kern/sleepq.h>
#include <arch/i386/idt.h>
#include <kern/console.h>
#include <kern/cmdline.h>
#include <kern/panic.h>
#include <stddef.h>
#include <pm/pm.h>
#include <exec/perso/personality.h>
#include <sys/core.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <kern/time.h>
#include <sys/kern_syscalls.h>

static void signal_interrupt_thread(thread_t *t) {
    if (!t) {
        return;
    }

    sleepq_remove_thread(t);
    t->sleep_status = -EINTR;
    t->wait_chan = NULL;
    t->state = THREAD_READY;
}

/*
 * Public counterpart for the directed-signal paths (thr_kill / thr_kill2):
 * wake `t` out of an interruptible sleep so a just-posted, unmasked signal
 * `sig` is taken now instead of only when the thread happens to return to
 * userspace.  No-op unless the thread is interruptibly blocked with `sig`
 * unmasked -- mirrors the wake condition psignal() uses for process-directed
 * signals.  Without this a pthread_kill() to a thread parked in an
 * interruptible syscall (e.g. FUTEX_WAIT) never delivers EINTR.
 */
void signal_wake_thread(thread_t *t, int sig) {
    if (!t || sig <= 0 || sig >= NSIG) {
        return;
    }
    uint32_t m = sigmask(sig);
    if (t->state == THREAD_BLOCKED &&
        (t->flags & THREAD_F_INTERRUPTIBLE) &&
        !(t->sig_mask & m)) {
        signal_interrupt_thread(t);
    }
}

static void signal_stop_process_threads(process_t *p, const char *reason) {
    if (!p) {
        return;
    }

    FOREACH_THREAD(thread) {
        if (thread->proc != p) continue;
        thread->state = THREAD_STOPPED;
        thread->wait_reason = reason;
    }
    p->state = SSTOP;
    /* A fresh stop is unreported: clear P_WAITED so wait4() surfaces it (each
     * ptrace stop must be visible to the tracer, not just the first). */
    p->p_flag &= (uint16_t)~P_WAITED;
}

void signal_resume_process_threads(process_t *p) {
    if (!p) {
        return;
    }

    FOREACH_THREAD(thread) {
        if (thread->proc != p) continue;
        if (thread->state == THREAD_STOPPED) {
            thread->state = THREAD_READY;
            thread->wait_reason = NULL;
        }
    }
    if (p->state == SSTOP) {
        p->state = SRUN;
    }
}

/* A process resumed from a job-control or ptrace stop must die at once if a
 * SIGKILL arrived while it was stopped: psignal() resumes it specifically to
 * deliver that kill, so it must not slip back to userspace with SIGKILL still
 * pending.  Without this, ^Z'd (stopped) processes survived SIGKILL until a
 * later SIGCONT finally let the long-pending signal be delivered. */
static void signal_die_if_killed(void) {
    if (current_thread && current_process &&
        (current_thread->sig_pending & sigmask(SIGKILL))) {
        signal_resume_process_threads(current_process);
        sigexit(current_process, SIGKILL);
    }
}

/* ptrace(2) helper: the saved user trapframe a stopped tracee will resume with
 * (its first thread's user_frame, captured at the signal-delivery stop).
 * Returns NULL if no thread of p is parked in a stop. */
void *ptrace_user_frame(process_t *p) {
    if (!p) {
        return NULL;
    }
    FOREACH_THREAD(thread) {
        if (thread->proc == p && thread->user_frame) {
            return thread->user_frame;
        }
    }
    return NULL;
}

/* ptrace exec-stop.  A freshly-exec'd traced process must stop at the entry of
 * the new image so its tracer can insert breakpoints before it runs.  Exec
 * enters userspace via jump_to_userspace() rather than an iret, so this can't
 * ride the normal signal-on-return path: elf_execve() hands us a trapframe it
 * built describing the new image's entry state, we park the thread here (the
 * frame lives on the stopped thread's kernel stack), and on resume the caller
 * jumps to the possibly tracer-modified frame->eip/useresp.  No-op if the
 * process isn't traced. */
void ptrace_exec_stop(struct registers *frame) {
    process_t *tracer;

    if (!current_process || !(current_process->p_flag & P_TRACED)) {
        return;
    }
    tracer = current_process->p_tracer ?
             current_process->p_tracer : current_process->p_parent;
    current_thread->user_frame = frame;
    current_process->p_xsig = (uint8_t)SIGTRAP;
    signal_stop_process_threads(current_process, "exec-stop");
    if (tracer) {
        psignal(tracer, SIGCHLD);
        sched_wakeup(&tracer->p_children);
    }
    sched_yield();
    signal_die_if_killed();
    current_thread->user_frame = NULL;
}

static void signal_clear_trap_context(thread_t *t, int sig) {
    if (!t || t->trap_signo != sig) {
        return;
    }

    t->trap_signo = 0;
    t->trap_code = 0;
    t->trap_addr = 0;
}

static int signal_core_dump_permitted(process_t *p) {
    if (!p) {
        return 0;
    }

    return p->rlimits[RLIMIT_CORE].rlim_cur != 0;
}

// Signal System Calls
int kern_sigaction(int sig, const struct sigaction *act, struct sigaction *oact) {
    if (sig <= 0 || sig > NSIG) return -EINVAL;
    /* SIGKILL/SIGSTOP can't have their action *changed*, but POSIX requires a
     * query (act == NULL) to succeed and report the current disposition — gdb
     * saves every signal's state this way at startup.  (Also: this used to
     * return a bare -1, which libc maps to EPERM; the correct error is EINVAL.) */
    if ((sig == SIGKILL || sig == SIGSTOP) && act) return -EINVAL;

    uint32_t mask = sigmask(sig);
    
    if (oact) *oact = current_process->sig_actions[sig - 1];
    
    if (act) {
        current_process->sig_actions[sig - 1] = *act;
        
        /* Update sig_catch and sig_ignore bitmasks */
        if (act->sa_handler == SIG_IGN) {
            current_process->sig_ignore |= mask;
            current_process->sig_catch &= ~mask;
        } else if (act->sa_handler == SIG_DFL) {
            current_process->sig_ignore &= ~mask;
            current_process->sig_catch &= ~mask;
        } else {
            /* Custom handler */
            current_process->sig_ignore &= ~mask;
            current_process->sig_catch |= mask;
        }
    }
    return 0;
}

int sys_sigaction(int sig, const void *act, void *oact) {
    struct sigaction kact, koact;
    struct sigaction *p_kact = NULL;

    /* Diagnostic: nosigalrm cmdline flag forces SIGALRM and
     * SIGIO to disposition SIG_IGN regardless of what userland
     * requests.  Use this to bisect whether the X server crash
     * is in async-signal installation/delivery or elsewhere.
     * SIGALRM and SIGIO are the two async signals X server uses
     * (smart scheduler and evdev input event notification
     * respectively); both reproduce the same "value should be
     * valid but is NULL+small" crash pattern.
     *
     * SIG_IGN — not "discard the install" — is the right
     * disposition: just discarding leaves the action at its
     * previous value (SIG_DFL = terminate for SIGIO), so the
     * first input event would kill the process.  SIG_IGN makes
     * psignal drop the signal silently. */
    if (sig == SIGALRM || sig == SIGIO) {
        static int nosigalrm_cached = -1;
        if (nosigalrm_cached < 0)
            nosigalrm_cached = (cmdline_has("nosigalrm") ||
                                cmdline_debug_enabled("nosigalrm")) ? 1 : 0;
        if (nosigalrm_cached) {
            if (oact) {
                struct sigaction zero = { 0 };
                if (copyout(&zero, oact, sizeof(zero)) != 0) return -14;
            }
            if (current_process) {
                struct sigaction ign = { 0 };
                ign.sa_handler = SIG_IGN;
                current_process->sig_actions[sig - 1] = ign;
            }
            return 0;
        }
    }

    if (act) {
        if (copyin(act, &kact, sizeof(struct sigaction)) != 0) return -14;
        p_kact = &kact;
    }

    /* DEBUG: detect FreeBSD __fail()'s sigaction(SIGABRT, SIG_DFL).
     * That call signals __stack_chk_fail (or __chk_fail) is being
     * invoked.  Capture the user-EIP / return-address chain so we can
     * cross-reference with the binary disasm and find which function
     * tripped the canary. */
    if (sig == 6 && p_kact && (uintptr_t)p_kact->sa_handler == 0 &&
        current_process &&
        (current_process->perso_id == PERS_FREEBSD ||
         current_process->perso_id == PERS_NETBSD)) {
        extern int kprintf(const char *fmt, ...);
        registers_t *r = current_thread ? (registers_t *)current_thread->syscall_regs : NULL;
        if (r) {
            kprintf("[ABORT] pid=%d eip=0x%08x esp=0x%08x ebp=0x%08x\n",
                    current_process->pid, r->eip, r->useresp, r->ebp);
            uint32_t us[8] = {0};
            copyin((const void *)(uintptr_t)r->useresp, us, sizeof(us));
            kprintf("[ABORT] stack: %08x %08x %08x %08x %08x %08x %08x %08x\n",
                    us[0], us[1], us[2], us[3],
                    us[4], us[5], us[6], us[7]);
            /* Walk the EBP chain to recover caller frames. */
            uint32_t fp = r->ebp;
            uint32_t victim_ebp = 0;
            for (int d = 0; d < 6 && fp >= 0x08000000 && fp < 0xC0000000; d++) {
                uint32_t frame[2] = {0};
                copyin((const void *)(uintptr_t)fp, frame, sizeof(frame));
                kprintf("[ABORT] frame[%d] ebp=0x%08x saved_ebp=0x%08x ret=0x%08x\n",
                        d, fp, frame[0], frame[1]);
                if (d == 3) victim_ebp = fp;  /* victim's frame */
                fp = frame[0];
            }
            /* Dump __stack_chk_guard from ls's data segment (its R_386_COPY
             * landing pad — known address from readelf).  Also dump the
             * canary slot on the victim's stack frame at -0x10(%ebp). */
            if (victim_ebp) {
                /* Dump a range under victim's ebp — the canary is the
                 * stash slot that should hold __stack_chk_guard[0].
                 * For the function at libc 0x84150 it's around -0x18
                 * but the exact offset depends on the function. */
                kprintf("[ABORT] victim ebp=0x%08x dump:\n", victim_ebp);
                for (int i = -8; i <= 0; i++) {
                    uint32_t vs = 0;
                    copyin((const void *)(uintptr_t)(victim_ebp + (uint32_t)(i * 4)), &vs, sizeof(vs));
                    kprintf("  ebp%+d (0x%08x) = 0x%08x\n",
                            i * 4, victim_ebp + i * 4, vs);
                }
            }
        }
    }

    int ret = kern_sigaction(sig, p_kact, oact ? &koact : NULL);
    if (ret == 0 && oact) {
        if (copyout(&koact, oact, sizeof(struct sigaction)) != 0) return -14;
    }
    return ret;
}

int kern_sigprocmask(int how, const uint32_t *set, uint32_t *oset) {
    if (oset) *oset = current_thread->sig_mask;
    if (set) {
        uint32_t new_set = *set;
        new_set &= ~(sigmask(SIGKILL) | sigmask(SIGSTOP));
        if (how == 1)      current_thread->sig_mask |= new_set;     // SIG_BLOCK
        else if (how == 2) current_thread->sig_mask &= ~new_set;    // SIG_UNBLOCK
        else if (how == 3) current_thread->sig_mask = new_set;      // SIG_SETMASK
        else return -EINVAL;   /* invalid 'how' when changing the mask
                                * (sigprocmask/17-1, pthread_sigmask/16-1) */
    }
    return 0;
}

int sys_sigprocmask(int how, const void *set, void *oset) {
    uint32_t kset, koset;
    if (set) {
        if (copyin(set, &kset, sizeof(uint32_t)) != 0) return -14;
    }
    int ret = kern_sigprocmask(how, set ? &kset : NULL, oset ? &koset : NULL);
    if (ret == 0 && oset) {
        if (copyout(&koset, oset, sizeof(uint32_t)) != 0) return -14;
    }
    return ret;
}

int kern_sigpending(uint32_t *set) {
    /* POSIX: sigpending(2) reports the set of signals pending on the
     * calling thread (or process), INCLUDING those currently blocked.
     * It must NOT be filtered by the signal mask — the whole purpose of
     * sigpending is to observe signals raised while blocked (OPTS
     * sigpending/1-1..1-3, and sigaction/23-* which raise a signal
     * inside its own handler, where it is masked, and expect
     * sigpending() to report it). */
    if (set) *set = current_thread->sig_pending;
    return 0;
}

int sys_sigpending(void *set) {
    uint32_t kset;
    int ret = kern_sigpending(&kset);
    if (ret == 0) {
        if (copyout(&kset, set, sizeof(uint32_t)) != 0) return -14;
    }
    return ret;
}

int kern_sigsuspend(const uint32_t *mask) {
    uint32_t old_mask = current_thread->sig_mask;
    if (mask) {
        current_thread->sig_mask = *mask;
    }

    /* Mark interruptible so psignal() can wake us */
    current_thread->flags |= THREAD_F_INTERRUPTIBLE;

    /* Sleep until a signal arrives that is not masked */
    while (!(current_thread->sig_pending & ~current_thread->sig_mask)) {
        sched_sleep(&current_thread->sig_pending);
    }

    current_thread->flags &= ~THREAD_F_INTERRUPTIBLE;

    /*
     * Do NOT restore sig_mask here.  signal_handle_pending runs after
     * we return to the syscall return path; it gates on
     * `sig_pending & ~sig_mask`.  If we restored old_mask now, that
     * mask would re-block the very signal that woke us, so
     * signal_handle_pending would observe no deliverable signal and
     * return to userspace without running the handler.  The pending
     * bit would stay set, the next sigsuspend would see it
     * immediately, and zsh-style "block + sigsuspend" loops would
     * spin in tight back-to-back syscalls (the symptom: every wait4
     * deadlock from a shell that uses sigsuspend to await SIGCHLD).
     *
     * Instead: keep sig_mask at the temporary suspend mask, stash
     * the pre-suspend mask in sig_mask_suspend, and let
     * signal_handle_pending pass *that* into the signal frame so
     * sigreturn restores the right thing.
     */
    current_thread->sig_mask_suspend = old_mask;
    current_thread->sig_mask_suspend_active = 1;
    return -1; /* Always returns -1 (EINTR) per POSIX */
}

int kern_sigaltstack(const stack_t *ss, stack_t *oss) {
    if (oss) {
        *oss = current_thread->sig_alt_stack;
        if (current_thread->sig_on_stack) {
            oss->ss_flags |= SS_ONSTACK;
        } else {
            oss->ss_flags &= ~SS_ONSTACK;
        }
    }
    
    if (ss) {
        if (current_thread->sig_on_stack) return -1; // EPERM/EINVAL
        if (ss->ss_flags & SS_DISABLE) {
             current_thread->sig_alt_stack.ss_sp = NULL;
             current_thread->sig_alt_stack.ss_size = 0;
             current_thread->sig_alt_stack.ss_flags = SS_DISABLE;
        } else {
             if (ss->ss_size < MINSIGSTKSZ) return -1; // ENOMEM/EINVAL
             current_thread->sig_alt_stack = *ss;
             current_thread->sig_alt_stack.ss_flags &= ~(SS_DISABLE | SS_ONSTACK);
        }
    }
    return 0;
}

int sys_sigaltstack(const void *ss, void *oss) {
    stack_t kss, koss;
    stack_t *p_kss = NULL;
    
    if (ss) {
        if (copyin(ss, &kss, sizeof(stack_t)) != 0) return -14;
        p_kss = &kss;
    }
    
    int ret = kern_sigaltstack(p_kss, oss ? &koss : NULL);
    if (ret == 0 && oss) {
        if (copyout(&koss, oss, sizeof(stack_t)) != 0) return -14;
    }
    return ret;
}

int sys_sigsuspend(const void *mask) {
    uint32_t kmask;
    if (mask) {
        if (copyin(mask, &kmask, sizeof(uint32_t)) != 0) return -14;
    }
    return kern_sigsuspend(mask ? &kmask : NULL);
}

/*
 * sys_sigwait - Synchronously wait for a signal
 *
 * Waits for any signal in 'set' to become pending.
 * Removes the signal from pending and returns it in '*sig'.
 * Does NOT invoke the signal handler - this is synchronous signal consumption.
 *
 * POSIX: Returns 0 on success, error number on failure.
 */
int sys_sigwait(const uint32_t *set, int *sig) {
    uint32_t kset;
    int ksig;
    if (copyin(set, &kset, sizeof(uint32_t)) != 0) return -14; // EFAULT
    int ret = kern_sigwait(&kset, &ksig);
    if (ret == 0) {
        if (copyout(&ksig, sig, sizeof(int)) != 0) return -14;
    }
    // POSIX sigwait returns error code, not -1 with errno.
    // However, our kern_sigwait might return 0 or error.
    return ret;
}

int kern_sigwait(const uint32_t *set, int *sig) {
    if (!set || !sig) return 22; /* EINVAL */
    
    uint32_t wait_mask = *set;
    
    /* Cannot wait for SIGKILL or SIGSTOP */
    wait_mask &= ~(sigmask(SIGKILL) | sigmask(SIGSTOP));
    
    if (wait_mask == 0) return 22; /* EINVAL - no valid signals */
    
    current_thread->flags |= THREAD_F_INTERRUPTIBLE;
    
    /* Block until a signal in wait_mask becomes pending */
    while (1) {
        uint32_t deliverable = current_thread->sig_pending & wait_mask;
        
        if (deliverable) {
            /* Find the first matching signal */
            for (int i = 0; i < NSIG; i++) {
                if (deliverable & (1 << i)) {
                    int signal = i + 1;
                    __sync_fetch_and_and(&current_thread->sig_pending, ~sigmask(signal));
                    *sig = signal;
                    current_thread->flags &= ~THREAD_F_INTERRUPTIBLE;
                    return 0;
                }
            }
        }
        
        sched_sleep(&current_thread->sig_pending);
        
        uint32_t other_pending = current_thread->sig_pending & ~current_thread->sig_mask & ~wait_mask;
        if (other_pending) {
            current_thread->flags &= ~THREAD_F_INTERRUPTIBLE;
            return 4; /* EINTR */
        }
    }
}

/*
 * sys_sigtimedwait - Timed wait for a signal with siginfo
 *
 * Like sigwait but with optional timeout and fills siginfo_t.
 *
 * POSIX: Returns signal number on success, -1 on error.
 */
int sys_sigtimedwait(const uint32_t *set, siginfo_t *info, 
                     const void *timeout) {
    uint32_t kset;
    siginfo_t kinfo;
    struct timespec kts;
    if (copyin(set, &kset, sizeof(uint32_t)) != 0) return -14;
    if (timeout) {
        if (copyin(timeout, &kts, sizeof(struct timespec)) != 0) return -14;
    }
    int ret = kern_sigtimedwait(&kset, info ? &kinfo : NULL, timeout ? &kts : NULL);
    if (ret > 0 && info) {
        if (copyout(&kinfo, info, sizeof(siginfo_t)) != 0) return -14;
    }
    return ret;
}

int kern_sigtimedwait(const uint32_t *set, siginfo_t *info,
                     const struct timespec *timeout) {
    if (!set) return -22; // EINVAL
    
    uint32_t wait_mask = *set;
    wait_mask &= ~(sigmask(SIGKILL) | sigmask(SIGSTOP));
    
    if (wait_mask == 0) return -22; // EINVAL
    
    uint64_t deadline = 0;
    int use_timeout = 0;

    if (timeout) {
        const struct timespec *ts = (const struct timespec *)timeout;
        if (ts->tv_nsec < 0 || ts->tv_nsec >= 1000000000) return -22; // EINVAL

        uint32_t hz = get_hz();
        uint64_t ticks = ts->tv_sec * hz;
        ticks += ((uint64_t)ts->tv_nsec * hz) / 1000000000;

        deadline = get_ticks() + ticks;
        use_timeout = 1;
    }
    
    /* Check for immediately available signal */
    uint32_t deliverable = current_thread->sig_pending & wait_mask;
    
    if (!deliverable && use_timeout) {
        uint64_t now = get_ticks();
        if (now >= deadline) {
             return -11; /* EAGAIN */
        }
    }
    
    current_thread->flags |= THREAD_F_INTERRUPTIBLE;
    
    /* Block until signal available (if no timeout) */
    while (!deliverable) {
        if (use_timeout) {
            uint64_t now = get_ticks();
            if (now >= deadline) {
                 current_thread->flags &= ~THREAD_F_INTERRUPTIBLE;
                 return -11; /* EAGAIN */
            }
            current_thread->sleep_expiry = deadline;
        }

        sched_sleep(&current_thread->sig_pending);

        if (use_timeout) {
            current_thread->sleep_expiry = 0;
        }

        deliverable = current_thread->sig_pending & wait_mask;
        
        uint32_t other_pending = current_thread->sig_pending & ~current_thread->sig_mask & ~wait_mask;
        if (other_pending) {
            current_thread->flags &= ~THREAD_F_INTERRUPTIBLE;
            return -4; /* EINTR */
        }
    }
    
    current_thread->flags &= ~THREAD_F_INTERRUPTIBLE;
    
    // Find the first matching signal
    int signal = 0;
    for (int i = 0; i < NSIG; i++) {
        if (deliverable & (1 << i)) {
            signal = i + 1;
            break;
        }
    }
    
    if (signal == 0) return -22; // Should not happen

    // Remove from pending (consume it)
    __sync_fetch_and_and(&current_thread->sig_pending, ~sigmask(signal));
    
    // Fill siginfo if provided
    if (info) {
        info->si_signo = signal;
        info->si_errno = 0;
        info->si_code = 0;        // Unknown source (synchronous wait)
        info->si_pid = 0;         // Unknown sender
        info->si_uid = 0;
        info->si_addr = NULL;
        info->si_status = 0;
    }
    
    return signal; // Return signal number on success
}

/*
 * psignal - Send signal to a process
 *
 * Delivers signal to the target process following POSIX/BSD semantics:
 * 1. Validates process pointer and signal number
 * 2. Protects init (PID 1) from fatal signals
 * 3. For SIGCONT, resumes stopped process/threads
 * 4. Sets pending bit on all threads (signal is process-directed)
 * 5. Selects best thread for immediate delivery (one not masking the signal)
 * 6. Wakes interruptibly-sleeping threads
 */
void psignal(process_t *p, int sig) {
    /* Validate process pointer and signal number */
    if (!p || sig <= 0 || sig > NSIG) return;

    /* Ignore signals if process is already exiting or a zombie */
    if (p->state == SDYING || p->state == SZOMB) {
        return;
    }
    
    /* Init Protection: Block SIGKILL/SIGTERM/SIGSTOP to PID 1 */
    if (p->pid == 1 && (sig == SIGKILL || sig == SIGTERM || sig == SIGSTOP)) {
        // Allow delivery if explicit handler is installed (not SIG_DFL)
        // Note: SIGKILL/SIGSTOP cannot usually be caught, so they remain blocked here
        // unless sys_sigaction laws change. SIGTERM can be caught.
        if (p->sig_actions[sig - 1].sa_handler == SIG_DFL) {
            return;
        }
    }

    /* Handle SIGCONT: Resume stopped process and clear pending stop signals */
    if (sig == SIGCONT) {
        uint32_t stop_mask = sigmask(SIGSTOP) | sigmask(SIGTSTP) | sigmask(SIGTTIN) | sigmask(SIGTTOU);

        // Clear pending stop signals on all threads (atomic — racing psignal on SMP)
        FOREACH_THREAD(thread) {
            if (thread->proc == p) {
                __sync_fetch_and_and(&thread->sig_pending, ~stop_mask);
            }
        }

        if (p->state == SSTOP) {
            signal_resume_process_threads(p);
            p->p_flag |= P_CONTINUED;
            p->p_flag &= (uint16_t)~P_WAITED;
            /* Notify parent if not SA_NOCLDSTOP */
            if (p->p_parent &&
                !(p->p_parent->sig_actions[SIGCHLD-1].sa_flags & SA_NOCLDSTOP)) {
                psignal(p->p_parent, SIGCHLD);
            }
            if (p->p_parent) {
                sched_wakeup(&p->p_parent->p_children);
            }
        }
    }

    /* SIGKILL must terminate even a stopped process.  Resume its threads so
     * they run signal_handle_pending() (and die) instead of lingering in
     * THREAD_STOPPED until a later SIGCONT delivers the long-pending kill. */
    if (sig == SIGKILL && p->state == SSTOP) {
        signal_resume_process_threads(p);
    }

    /* For SIGSTOP/SIGTSTP/SIGTTIN/SIGTTOU, clear SIGCONT */
    if (sig == SIGSTOP || sig == SIGTSTP || sig == SIGTTIN || sig == SIGTTOU) {
        FOREACH_THREAD(thread) {
            if (thread->proc == p) {
                __sync_fetch_and_and(&thread->sig_pending, ~sigmask(SIGCONT));
            }
        }
    }

    /* A signal whose effective disposition is "ignore" is discarded
     * here — it must never be left pending.  sched_sleep() treats any
     * pending unmasked signal as a reason to abort an interruptible
     * sleep, so a pending-but-ignored signal turns every blocking
     * read/accept/connect into a spurious -EINTR.  A child exit posts
     * SIGCHLD (default action: ignore); leaving it pending was making
     * a forked worker that inherited it busy-spin in read() forever.
     * SIGCONT has already done its resume work above, so dropping its
     * pending bit when un-handled is correct too. */
    {
        sig_t h = p->sig_actions[sig - 1].sa_handler;
        int ignored = (h == SIG_IGN ||
                       (h == SIG_DFL && (sigprop[sig] & PROP_IGNORE)));
        if (ignored) {
            /* An ignored signal is normally discarded rather than left
             * pending (see above: a pending-but-ignored signal aborts
             * every interruptible sleep).  BUT POSIX requires a *blocked*
             * signal to remain pending until it is unblocked — even when
             * its disposition is ignore — so sigpending(2) can report it.
             * OPTS sigpending/1-2,1-3 raise a blocked SIGCONT inside a
             * handler and expect it pending.  So: discard only when some
             * thread of the target can take the signal now (has it
             * unblocked); if every thread blocks it, keep it pending. */
            uint32_t m = sigmask(sig);
            int deliverable_now = 0;
            FOREACH_THREAD(t) {
                if (t->proc != p) continue;
                if (!(t->sig_mask & m)) { deliverable_now = 1; break; }
            }
            if (deliverable_now)
                return;
        }
    }

    /* Select best thread for delivery and set pending on all threads */
    uint32_t sig_mask = sigmask(sig);
    
    thread_t *best_thread = NULL;
    int best_priority = -1; // Higher is better

    FOREACH_THREAD(t) {
        if (t->proc != p) continue;

        /* SIGCONT Special: Wake up stopped threads */
        if (sig == SIGCONT && t->state == THREAD_STOPPED) {
            t->state = THREAD_READY;
        }
        
        /* Set pending bit on ALL threads (signal is process-directed).
         * Atomic OR since another CPU may concurrently psignal the same
         * thread, or the thread itself may be clearing bits during signal
         * delivery. */
        __sync_fetch_and_or(&t->sig_pending, sig_mask);
        
        /* Select best thread for immediate delivery:
         * Priority order:
         * 3: Running/Ready thread with signal unmasked
         * 2: Blocked (interruptible) thread with signal unmasked
         * 1: Any thread with signal unmasked
         * 0: Any thread (fallback)
         */
        int priority = 0;
        int unmasked = !(t->sig_mask & sig_mask);
        
        if (unmasked) {
            if (t->state == THREAD_RUNNING || t->state == THREAD_READY) {
                priority = 3;
            } else if (t->state == THREAD_BLOCKED &&
                       (t->flags & THREAD_F_INTERRUPTIBLE)) {
                priority = 2;
            } else {
                priority = 1;
            }
        }
        
        if (priority > best_priority) {
            best_priority = priority;
            best_thread = t;
        }
        
        /* Wake interruptibly-sleeping threads */
        if (t->state == THREAD_BLOCKED && unmasked &&
            (t->flags & THREAD_F_INTERRUPTIBLE)) {
            signal_interrupt_thread(t);
        }
    }
    
    /* If we found a best thread and it's blocked, wake it */
    if (best_thread && best_thread->state == THREAD_BLOCKED && 
        best_priority > 0 &&
        (best_thread->flags & THREAD_F_INTERRUPTIBLE)) {
        signal_interrupt_thread(best_thread);
    }
}

// Send signal to process group
void pgsignal(int pgrp_id, int sig) {
    if (pgrp_id <= 0 || sig <= 0 || sig > NSIG) return;
    
    /* Use new struct pgrp API from pgrp.c */
    extern struct pgrp *pgrp_find(int pgid);
    extern void pgrp_signal(struct pgrp *pgrp, int sig);
    
    struct pgrp *pgrp = pgrp_find(pgrp_id);
    if (pgrp) {
        pgrp_signal(pgrp, sig);
    }
}

/*
 * trapsignal - Send synchronous trap signal
 *
 * Called from exception handlers (page fault, divide by zero, illegal insn, etc.)
 * Unlike psignal, this:
 * 1. Forces delivery to the CURRENT thread (the one that caused the trap)
 * 2. Stores trap code in per-thread siginfo for SA_SIGINFO handlers
 * 3. Signals are synchronous - they must be handled immediately
 *
 * Typical sources:
 * - SIGSEGV: Page Fault (code = fault address or type)
 * - SIGFPE: Division by Zero, Overflow (code = FPE_INTDIV, FPE_FLTDIV, etc.)
 * - SIGILL: Illegal Instruction (code = ILL_ILLOPC, ILL_PRVOPC, etc.)
 * - SIGBUS: Bus Error (code = BUS_ADRALN, etc.)
 * - SIGTRAP: Breakpoint (code = TRAP_BRKPT)
 */
void trapsignal(process_t *p, int sig, int code) {
    if (!p || sig <= 0 || sig > NSIG) return;
    
    /* For trap signals, deliver to current thread specifically.
     * current_thread is the faulting thread that caused the exception. */
    if (current_thread && current_thread->proc == p) {
        /* Store trap info in thread's pending siginfo for later
         * delivery.  trap_addr is the caller's responsibility — the
         * trap dispatch site has cr2 / instruction pointer / etc.
         * Do NOT zero it here: every caller already assigns
         * trap_addr before invoking us, and unconditionally
         * clobbering it was making userspace CORE dumps always
         * show trap_addr=0x00000000. */
        current_thread->trap_signo = sig;
        current_thread->trap_code = code;


        /* Set signal pending on this specific thread (atomic — psignal
         * may also be modifying sig_pending from another CPU) */
        __sync_fetch_and_or(&current_thread->sig_pending, sigmask(sig));
        
        /* For synchronous signals, we don't need to wake - the signal
         * will be handled when returning from the exception handler.
         * If thread was blocked, we still mark it pending. */
    } else {
        /* Fallback: if current_thread doesn't match, use psignal */
        psignal(p, sig);
    }
}

/*
 * sigexit - Terminate process due to signal
 *
 * Called when a signal's default action is to terminate the process.
 * Sets exit status to indicate signal termination and optionally generates
 * a core dump.
 *
 * Exit status encoding (POSIX wait macros):
 * - WIFSIGNALED(status) is true
 * - WTERMSIG(status) returns the signal number
 * - WCOREDUMP(status) indicates core was dumped
 */
void sigexit(process_t *p, int sig) {
    if (!p || sig <= 0 || sig > NSIG) return;
    
    int want_core = (sigprop[sig] & PROP_CORE) != 0;
    int dump_core = 0;

    if (want_core && signal_core_dump_permitted(p)) {
        core_prepare_dump(p, sig);
        dump_core = coredump(p) == 0;
    }
    
    /* Set exit status to indicate signal termination:
     * POSIX encoding: signal number in bits 0-6, core dump in bit 7
     * This makes WIFSIGNALED(status) true and WTERMSIG return sig
     */
    int exit_status = sig & 0x7F; // Signal number in low 7 bits
    if (dump_core) {
        exit_status |= 0x80; // Set core dump bit
    }
    
    /* Terminate the process */
    extern void proc_exit(int status);
    p->p_flag |= P_SIGEXIT;
    p->exit_code = exit_status;
    proc_exit(exit_status);
}

static int signal_can_send(process_t *caller, process_t *target) {
    if (!target) {
        return 0;
    }
    if (!caller) {
        return 1;
    }

    if (caller->uid == 0 || caller->euid == 0) {
        return 1;
    }

    return caller->uid == target->uid ||
           caller->uid == target->euid ||
           caller->euid == target->uid ||
           caller->euid == target->euid;
}

static void signal_record_match(process_t *target, int *matched, int *permitted,
                                int sig) {
    if (!target || target->pid <= 0) {
        return;
    }

    if (matched) {
        (*matched)++;
    }

    if (!signal_can_send(current_process, target)) {
        return;
    }

    if (permitted) {
        (*permitted)++;
    }

    if (sig != 0) {
        psignal(target, sig);
    }
}

int sys_kill(int pid, int sig) {
    if (sig < 0 || sig > NSIG) return -EINVAL;

    // Handle PID > 0: Send to specific process
    if (pid > 0) {
        process_t *target = NULL;
        // Search process list
        target = proc_find(pid);

        if (!target) return -ESRCH;
        if (!signal_can_send(current_process, target)) {
            return -EPERM;
        }

        if (sig != 0) {
            psignal(target, sig);
        }
        return 0;
    }
    else if (pid == 0) {
        // Send to current process group
        if (!current_process || !current_process->p_pgrp) return -ESRCH;
        int permitted = 0;
        int matched = 0;
        process_t *member = current_process->p_pgrp->pg_members;
        while (member) {
            signal_record_match(member, &matched, &permitted, sig);
            member = member->p_pgrp_link;
        }
        if (matched == 0) return -ESRCH;
        if (permitted == 0) return -EPERM;
        return 0;
    }
    else if (pid == -1) {
        // Send to all processes (except Init)
        int permitted = 0;
        int matched = 0;
        FOREACH_PROC(proc) {
            if (proc->pid > 1) {
                signal_record_match(proc, &matched, &permitted, sig);
            }
        }
        if (matched == 0) return -ESRCH;
        if (permitted == 0) return -EPERM;
        return 0;
    }
    else {
        // pid < -1: Send to process group -pid
        extern struct pgrp *pgrp_find(int pgid);
        struct pgrp *pgrp = pgrp_find(-pid);
        int permitted = 0;
        int matched = 0;

        if (!pgrp) return -ESRCH;

        process_t *member = pgrp->pg_members;
        while (member) {
            signal_record_match(member, &matched, &permitted, sig);
            member = member->p_pgrp_link;
        }
        if (matched == 0) return -ESRCH;
        if (permitted == 0) return -EPERM;
        return 0;
    }
}

int signal_send_group(int pgrp, int sig) {
    pgsignal(pgrp, sig);
    return 0;
}

/*
 * sys_sigqueue - Queue a signal with an accompanying data value (sigqueue(2)).
 *
 * Sends `sig` to process `pid`, recording the `sival` payload so that an
 * SA_SIGINFO handler receives it as siginfo.si_value with si_code SI_QUEUE.
 * Substrate's pending set is a bitmask (signals do not truly queue), so the
 * payload is stored one-per-signal-number on the target process; the most
 * recent queued value wins if the same signal is queued repeatedly before
 * delivery.  sig == 0 performs only the permission/existence check.
 */
int sys_sigqueue(int pid, int sig, uintptr_t sival) {
    if (sig < 0 || sig > NSIG) return -EINVAL;
    /* POSIX sigqueue() addresses a single process by (positive) pid. */
    if (pid <= 0) return -EINVAL;

    process_t *target = proc_find(pid);
    if (!target) return -ESRCH;
    if (!signal_can_send(current_process, target)) return -EPERM;

    if (sig != 0) {
        target->sig_qval[sig - 1].sival_ptr = (void *)sival;
        __sync_fetch_and_or(&target->sig_qpend, sigmask(sig));
        psignal(target, sig);
    }
    return 0;
}

void signal_handle_pending(registers_t *regs) {
    if (!current_thread || !current_process) return;

    // Only deliver signals when returning to user mode
    // (Assuming Ring 3 is 0x1B or similar, but for now we check if cs != 0x08)
    if (regs->cs == 0x08) return; 
 

    uint32_t pending = current_thread->sig_pending & ~current_thread->sig_mask;
    if (pending == 0) return;

    // Find the first pending signal
    int sig = 0;
    for (int i = 0; i < NSIG; i++) {
        if (pending & (1 << i)) {
            sig = i + 1;
            break;
        }
    }

    if (sig == 0) return;

    // Clear pending bit (atomic — concurrent psignal may set bits)
    __sync_fetch_and_and(&current_thread->sig_pending, ~sigmask(sig));

    /* POSIX.1b timer overrun: this signal is now being accepted/delivered,
     * so latch the overrun count of any per-process timer that generated it
     * (timer_getoverrun(2)).  Cheap no-op when the process has no timers. */
    ptimer_signal_delivered(current_process, sig);

    // Special Handling: SIGKILL always terminates immediately
    if (sig == SIGKILL) {
        signal_resume_process_threads(current_process);
        sigexit(current_process, SIGKILL);
        return; // Should not reach
    }

    /* ptrace signal-delivery stop: a traced process stops on every signal
     * except SIGKILL and reports it to its tracer, which then inspects and
     * resumes it via ptrace(2).  The signal is consumed here; the tracer can
     * re-request delivery through PTRACE_CONT's data argument.  user_frame is
     * captured so PTRACE_GETREGS/SETREGS see the frame it will resume with. */
    if ((current_process->p_flag & P_TRACED) && sig != SIGKILL) {
        process_t *tracer = current_process->p_tracer ?
                            current_process->p_tracer : current_process->p_parent;
        current_thread->user_frame = regs;
        current_process->p_xsig = (uint8_t)sig;
        signal_stop_process_threads(current_process, "ptrace-stop");
        if (tracer) {
            psignal(tracer, SIGCHLD);
            sched_wakeup(&tracer->p_children);
        }
        signal_clear_trap_context(current_thread, sig);
        sched_yield();
        signal_die_if_killed();
        return;
    }

    struct sigaction *act = &current_process->sig_actions[sig - 1];

    if (act->sa_handler == SIG_IGN) {
        signal_clear_trap_context(current_thread, sig);
        return;
    } else if (act->sa_handler == SIG_DFL) {
        // Default actions
        if (sig == SIGSEGV || sig == SIGILL || sig == SIGFPE || sig == SIGBUS || sig == SIGTRAP) {
            core_capture_trapframe(current_process, regs);
            signal_clear_trap_context(current_thread, sig);
            sigexit(current_process, sig);
            return;
        }

        if (sig == SIGINT || sig == SIGTERM) {
            // Init Protection: PID 1 ignores external fatal signals with default action
            signal_clear_trap_context(current_thread, sig);
            if (current_process->pid == 1) return;
            sigexit(current_process, sig);
            return;
        }
        
        // Job Control Stops
        if (sig == SIGSTOP || sig == SIGTSTP || sig == SIGTTIN || sig == SIGTTOU) {
             /* An orphaned process group discards the *terminal-generated*
              * job-control stops (SIGTSTP/SIGTTIN/SIGTTOU) so it can't wedge
              * itself unkillable with no shell to continue it.  SIGSTOP is
              * never discardable (POSIX) — an explicit SIGSTOP always stops,
              * orphaned or not. */
             extern int pgrp_is_orphaned(struct pgrp *pgrp);
             if (sig != SIGSTOP && current_process->p_pgrp &&
                 pgrp_is_orphaned(current_process->p_pgrp)) {
                 return;
             }

             current_process->p_xsig = (uint8_t)sig;
             signal_stop_process_threads(current_process, "Signal");
             
             // Notify parent if not SA_NOCLDSTOP
             if (current_process->p_parent &&
                 !(current_process->p_parent->sig_actions[SIGCHLD-1].sa_flags & SA_NOCLDSTOP)) {
                 psignal(current_process->p_parent, SIGCHLD);
             }
             if (current_process->p_parent) {
                 sched_wakeup(&current_process->p_parent->p_children);
             }
             signal_clear_trap_context(current_thread, sig);
             sched_yield();
             /* Resumed.  A SIGKILL that landed while we were stopped resumes
              * us here (via psignal); honour it now rather than returning to
              * userspace with the kill still pending. */
             signal_die_if_killed();
             return;
        }
        
        // Ignore others by default (like SIGCHLD, SIGCONT if not stopped)
        signal_clear_trap_context(current_thread, sig);
        return;
    }

    // Capture handler config
    sig_t handler = act->sa_handler;
    uint32_t flags = act->sa_flags;

    /*
     * Two mask values to track separately:
     *
     *   pre_handler_mask — what sig_mask is *right now* (which is
     *     the temporary suspend-mask if we're delivering during a
     *     sigsuspend, otherwise the regular thread mask).  This is
     *     the base for the during-handler mask per POSIX.
     *
     *   restore_mask — what sigreturn must put back into sig_mask
     *     when the handler returns.  Normally same as pre_handler_mask,
     *     but if sig_mask_suspend_active is set we must use the
     *     pre-sigsuspend mask saved there instead — otherwise the
     *     temporary suspend mask leaks past sigreturn.
     */
    uint32_t pre_handler_mask = current_thread->sig_mask;
    uint32_t restore_mask;
    if (current_thread->sig_mask_suspend_active) {
        restore_mask = current_thread->sig_mask_suspend;
        current_thread->sig_mask_suspend_active = 0;
        current_thread->sig_mask_suspend = 0;
    } else {
        restore_mask = pre_handler_mask;
    }

    // SA_NODEFER: Block the signal itself unless requested not to
    uint32_t new_mask = pre_handler_mask | act->sa_mask;
    if (!(flags & SA_NODEFER)) {
        new_mask |= sigmask(sig);
    }
    /* SIGKILL and SIGSTOP can never be blocked — not even transiently
     * while a handler runs.  A caught signal's sa_mask may legitimately
     * name them (OPTS sigaction/4-*: install a handler with SIGKILL /
     * SIGSTOP in sa_mask, raise it, then raise SIGKILL/SIGSTOP from
     * inside the handler and expect the process to be killed/stopped).
     * Strip them from the during-handler mask so such a signal is still
     * delivered. */
    new_mask &= ~(sigmask(SIGKILL) | sigmask(SIGSTOP));
    current_thread->sig_mask = new_mask;

    // SA_RESETHAND: Reset to SIG_DFL
    if (flags & SA_RESETHAND) {
        struct sigaction dfl;
        dfl.sa_handler = SIG_DFL;
        dfl.sa_flags = 0;
        dfl.sa_mask = 0;
        kern_sigaction(sig, &dfl, NULL);
    }

    // SA_RESTART: Restart syscall if it was interrupted and handler has SA_RESTART
    // EINTR is typically 4 (Linux/Native) or -4/4 (FreeBSD)
    // On i386, we decrement EIP by 2 (size of INT 0x80) and restore EAX
    if (current_thread->in_syscall && (int32_t)regs->eax == -4) {
        if (flags & SA_RESTART) {
            regs->eax = current_thread->syscall_orig_eax;
            regs->eip -= 2; // Size of INT 0x80 opcode
        }
    }

    // Deliver signal via personality-specific sendsig.  Pass
    // restore_mask (not pre_handler_mask) so sigreturn restores the
    // correct pre-signal/pre-sigsuspend mask.
    struct personality *p = perso_lookup(current_process->perso_id);
    if (p && p->sendsig) {
        p->sendsig((void*)handler, sig, restore_mask, flags, regs);
    } else {
        // Fallback to native sendsig if no personality hook (should not happen for valid perso)
        extern void sendsig(sig_t handler, int sig, uint32_t mask, uint32_t flags, registers_t *regs);
        sendsig(handler, sig, restore_mask, flags, regs);
    }
    signal_clear_trap_context(current_thread, sig);

    // Set P_CONTINUED was already done in psignal for SIGCONT, 
    // but if we delivered it to a handler, the app is "officially" continued.
}
