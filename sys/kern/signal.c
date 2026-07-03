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
#include <string.h>
#include <pm/pm.h>
#include <exec/perso/personality.h>
#include <sys/core.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <kern/time.h>
#include <sys/kern_syscalls.h>

/*
 * POSIX real-time signal queue (the per-process rtsig_q[] in sys/proc.h).
 * Signals in [SIGRTMIN,SIGRTMAX] queue as distinct instances rather than
 * collapsing into the sig_pending bitmask.  These two helpers are the only
 * code that mutates rtsig_q[]/rtsig_count/rtsig_seq; both take rtsig_lock
 * with local IRQs disabled because psignal() (hence rtsig_enqueue) may run
 * from an interrupt handler — e.g. a TTY ^C posting SIGINT is fine, but the
 * same path posts RT signals too, and an IRQ landing on a CPU already
 * holding the lock would trip the spinlock deadlock check.
 */

/*
 * rtsig_enqueue - queue one real-time signal instance on process `p`.
 *
 * Returns 0 on success, or -EAGAIN if the queue is full (RTSIG_QUEUE_MAX
 * instances already pending) — the POSIX sigqueue(2) overflow error.
 */
static int rtsig_enqueue(process_t *p, int sig, int code, union sigval val) {
    int ret = -EAGAIN;
    /* Record the sender (the process making the sigqueue/kill call) so the
     * delivered siginfo reports si_pid/si_uid of the SENDER, not the receiver. */
    int32_t  s_pid = current_process ? (int32_t)current_process->pid : 0;
    uint32_t s_uid = current_process ? current_process->uid : 0;
    unsigned long fl = spinlock_acquire_irq(&p->rtsig_lock);
    if (p->rtsig_count < RTSIG_QUEUE_MAX) {
        for (int i = 0; i < RTSIG_QUEUE_MAX; i++) {
            if (p->rtsig_q[i].rt_signo == 0) {
                p->rtsig_q[i].rt_signo = (uint16_t)sig;
                p->rtsig_q[i].rt_code  = (int16_t)code;
                /* Per-process monotonic FIFO key.  ++ before use so an
                 * occupied slot's rt_seq is never 0 (0xFFFFFFFF wrap would
                 * take 4 billion RT posts in one process — not a concern). */
                p->rtsig_q[i].rt_seq   = ++p->rtsig_seq;
                p->rtsig_q[i].rt_value = val;
                p->rtsig_q[i].rt_pid   = s_pid;
                p->rtsig_q[i].rt_uid   = s_uid;
                p->rtsig_count++;
                ret = 0;
                /* Set the per-thread pending bit for every thread of p while
                 * STILL holding rtsig_lock.  This serializes the enqueue
                 * against signal_handle_pending()/sigwait_consume()'s
                 * take-last-instance -> clear-bit decision, which now also
                 * runs under this lock (see rtsig_dequeue).  Without the
                 * serialization an enqueue landing between "took the last
                 * instance" and "cleared the bit" would strand the new
                 * instance: bit clear, queue non-empty -> lost signal + a
                 * permanently-occupied slot (spurious EAGAIN). */
                uint32_t m = sigmask(sig);
                FOREACH_THREAD(t) {
                    if (t->proc == p)
                        __sync_fetch_and_or(&t->sig_pending, m);
                }
                break;
            }
        }
    }
    spinlock_release_irq(&p->rtsig_lock, fl);
    return ret;
}

/*
 * rtsig_dequeue - remove the oldest queued instance of `sig` from `p`.
 *
 * On success fills *code / *val from that instance, returns 1, and sets
 * *more nonzero iff another instance of the SAME signo remains queued (so
 * the caller knows whether to keep the per-thread pending bit set).  FIFO
 * within a signo is enforced by picking the lowest rt_seq.  Returns 0 when
 * the queue holds nothing for `sig` — the benign race where another thread
 * of a multithreaded process drained the last instance first.
 *
 * `pid`/`uid`, when non-NULL, receive the sender's pid/ruid recorded at
 * enqueue (siginfo si_pid/si_uid — the SENDER, not the receiver).
 *
 * `clear_thread`, when non-NULL, is the caller's thread: iff the queue holds
 * no more instances of `sig` after this dequeue (including the not-found
 * case), its per-thread pending bit for `sig` is cleared HERE, under
 * rtsig_lock.  Doing the take-last -> clear-bit decision atomically under the
 * lock — paired with rtsig_enqueue setting the bit under the same lock —
 * closes the lost/stranded-signal race.
 */
static int rtsig_dequeue(process_t *p, int sig, int *code,
                         union sigval *val, int *more,
                         thread_t *clear_thread, int *pid, uint32_t *uid) {
    int found = -1;
    uint32_t best_seq = 0xFFFFFFFFu;
    int remaining = 0;

    unsigned long fl = spinlock_acquire_irq(&p->rtsig_lock);
    for (int i = 0; i < RTSIG_QUEUE_MAX; i++) {
        if (p->rtsig_q[i].rt_signo == (uint16_t)sig &&
            p->rtsig_q[i].rt_seq < best_seq) {
            best_seq = p->rtsig_q[i].rt_seq;
            found = i;
        }
    }
    if (found >= 0) {
        if (code) *code = p->rtsig_q[found].rt_code;
        if (val)  *val  = p->rtsig_q[found].rt_value;
        if (pid)  *pid  = p->rtsig_q[found].rt_pid;
        if (uid)  *uid  = p->rtsig_q[found].rt_uid;
        p->rtsig_q[found].rt_signo = 0;
        if (p->rtsig_count > 0) p->rtsig_count--;
        for (int i = 0; i < RTSIG_QUEUE_MAX; i++) {
            if (p->rtsig_q[i].rt_signo == (uint16_t)sig) { remaining = 1; break; }
        }
    }
    /* Clear the caller-thread's pending bit iff no instance of `sig` remains
     * (found<0 implies remaining==0), serialized under rtsig_lock against a
     * concurrent enqueue that sets the bit under the same lock. */
    if (clear_thread && !remaining) {
        __sync_fetch_and_and(&clear_thread->sig_pending, ~sigmask(sig));
    }
    spinlock_release_irq(&p->rtsig_lock, fl);

    if (more) *more = remaining;
    return found >= 0;
}

/*
 * sigwait_consume - synchronously consume one pending instance of `sig`.
 *
 * Used by sigwait(2)/sigtimedwait(2), which accept a signal without running
 * its handler.  Mirrors the source-of-truth bookkeeping that
 * signal_handle_pending() does for a handler delivery, so a synchronously
 * accepted signal is accounted for identically:
 *
 *  - A POSIX.1b timer notification (sig_timer_pend set): latch the timer's
 *    overrun via ptimer_signal_delivered(), report si_code == SI_TIMER with
 *    the sigev_value, and clear sig_timer_pend.  Without this a periodic
 *    timer accepted through sigwait went silent after one shot
 *    (sig_outstanding stuck at 1) and the RT signo stayed poisoned forever.
 *  - A real-time signal: dequeue one rtsig_q[] instance (reporting
 *    si_code/si_value/si_pid/si_uid), leaving the pending bit set while more
 *    instances remain; the take-last -> clear-bit decision runs under
 *    rtsig_lock (see rtsig_dequeue).
 *  - A standard sigqueue(2) instance (sig_qpend set): report si_code
 *    SI_QUEUE with the stored value and consume the marker.
 *  - Anything else: just clear the per-thread pending bit.
 *
 * code/val/pid/uid receive the reported siginfo fields when non-NULL.
 */
static void sigwait_consume(int sig, int *code, union sigval *val,
                            int *pid, uint32_t *uid) {
    if (pid) *pid = 0;
    if (uid) *uid = 0;

    /* Timer notification takes precedence (it may ride an RT signo). */
    if (current_process && sig >= 1 && sig < NSIG &&
        (current_process->sig_timer_pend & sigmask(sig))) {
        if (code) *code = SI_TIMER;
        if (val)  *val  = current_process->sig_qval[sig - 1];
        ptimer_signal_delivered(current_process, sig);
        __sync_fetch_and_and(&current_process->sig_timer_pend, ~sigmask(sig));
        __sync_fetch_and_and(&current_thread->sig_pending, ~sigmask(sig));
        return;
    }

    if (SIG_IS_RT(sig)) {
        int more = 0;
        /* rtsig_dequeue clears our pending bit under rtsig_lock iff no
         * instance remains; leaves it set otherwise. */
        rtsig_dequeue(current_process, sig, code, val, &more,
                      current_thread, pid, uid);
        return;
    }

    /* Standard signal: surface a queued sigqueue(2) payload if present. */
    if (current_process && sig >= 1 && sig < NSIG &&
        (current_process->sig_qpend & sigmask(sig))) {
        if (code) *code = SI_QUEUE;
        if (val)  *val  = current_process->sig_qval[sig - 1];
        __sync_fetch_and_and(&current_process->sig_qpend, ~sigmask(sig));
    }
    __sync_fetch_and_and(&current_thread->sig_pending, ~sigmask(sig));
}

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
    /* Wake if the signal is unmasked (normal delivery) OR the thread is
     * synchronously waiting for it in sigwait/sigtimedwait (which masks the
     * awaited signal on purpose) — in the latter case the mask must NOT
     * suppress the wake, or the waiter sleeps the full timeout. */
    if (t->state == THREAD_BLOCKED &&
        (t->flags & THREAD_F_INTERRUPTIBLE) &&
        (!(t->sig_mask & m) || (t->sig_wait_mask & m))) {
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
    if (sig <= 0 || sig >= NSIG) return -EINVAL;
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
    /* Advertise the awaited set so psignal()/thr_kill() wake us even for a
     * masked signal (sigwait accepts normally-blocked signals). */
    current_thread->sig_wait_mask = wait_mask;

    /* Block until a signal in wait_mask becomes pending */
    while (1) {
        uint32_t deliverable = current_thread->sig_pending & wait_mask;

        if (deliverable) {
            /* Find the first matching signal */
            for (int i = 0; i < NSIG; i++) {
                if (deliverable & (1 << i)) {
                    int signal = i + 1;
                    sigwait_consume(signal, NULL, NULL, NULL, NULL);
                    *sig = signal;
                    current_thread->sig_wait_mask = 0;
                    current_thread->flags &= ~THREAD_F_INTERRUPTIBLE;
                    return 0;
                }
            }
        }

        sched_sleep(&current_thread->sig_pending);

        uint32_t other_pending = current_thread->sig_pending & ~current_thread->sig_mask & ~wait_mask;
        if (other_pending) {
            current_thread->sig_wait_mask = 0;
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
    /* kern_sigtimedwait fills only the named siginfo_t fields; zero the whole
     * struct (incl. _pad[25]) so the full-sizeof copyout below cannot leak
     * uninitialized kernel stack to userspace. */
    memset(&kinfo, 0, sizeof(kinfo));
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
        /* POSIX: a negative tv_sec (as well as an out-of-range tv_nsec) is
         * EINVAL.  Without the tv_sec check a negative seconds value fed the
         * tick math a huge unsigned product and returned a spurious EAGAIN. */
        if (ts->tv_sec < 0 || ts->tv_nsec < 0 || ts->tv_nsec >= 1000000000)
            return -22; // EINVAL

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
    /* Advertise the awaited set so psignal()/thr_kill() wake us on arrival
     * even for a masked signal — a timed wait masks the signals it awaits, so
     * the plain "unmasked" wake condition would never fire and we'd sleep the
     * whole timeout instead of returning when the signal arrives. */
    current_thread->sig_wait_mask = wait_mask;

    /* Block until signal available (if no timeout) */
    while (!deliverable) {
        if (use_timeout) {
            uint64_t now = get_ticks();
            if (now >= deadline) {
                 current_thread->sig_wait_mask = 0;
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
            current_thread->sig_wait_mask = 0;
            current_thread->flags &= ~THREAD_F_INTERRUPTIBLE;
            return -4; /* EINTR */
        }
    }

    current_thread->sig_wait_mask = 0;
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

    /* Consume one instance.  sigwait_consume reports the correct si_code
     * (SI_TIMER for a timer notification, SI_QUEUE for a queued sigqueue(2)
     * instance, SI_USER otherwise), the si_value payload, and the sender's
     * si_pid/si_uid for a real-time instance; it also does the timer overrun
     * / rtsig_q[] bookkeeping so a synchronously accepted signal is accounted
     * for exactly like a handler delivery. */
    int wcode = 0;
    union sigval wval = { .sival_int = 0 };
    int wpid = 0;
    uint32_t wuid = 0;
    sigwait_consume(signal, &wcode, &wval, &wpid, &wuid);

    // Fill siginfo if provided
    if (info) {
        info->si_signo = signal;
        info->si_errno = 0;
        info->si_code = wcode;
        info->si_pid = wpid;
        info->si_uid = wuid;
        info->si_addr = NULL;
        info->si_status = 0;
        info->si_value = wval;
    }

    return signal; // Return signal number on success
}

/*
 * psignal_info - Send signal to a process, carrying siginfo (si_code/value).
 *
 * The workhorse behind both psignal() (si_code SI_USER, no payload) and the
 * sigqueue(2) SI_QUEUE path.  Delivers `sig` to `p` following POSIX/BSD
 * semantics:
 * 1. Validates process pointer and signal number
 * 2. Protects init (PID 1) from fatal signals
 * 3. For SIGCONT, resumes stopped process/threads
 * 4. For a real-time signal ([SIGRTMIN,SIGRTMAX]) that is not a pending
 *    timer notification, enqueues a distinct instance on rtsig_q[]
 * 5. Sets pending bit on all threads (signal is process-directed)
 * 6. Selects best thread for immediate delivery (one not masking the signal)
 * 7. Wakes interruptibly-sleeping threads
 *
 * Returns 0 normally; -EAGAIN only when an SI_QUEUE enqueue overflows the
 * RT queue (so sigqueue(2) can report it).  A full queue on the SI_USER
 * (kill) path is best-effort: the signal still posts (the already-queued
 * instances will deliver), and 0 is returned.
 */
static int psignal_info(process_t *p, int sig, int si_code,
                        const union sigval *valp) {
    /* Validate process pointer and signal number */
    if (!p || sig <= 0 || sig >= NSIG) return 0;

    /* Ignore signals if process is already exiting or a zombie */
    if (p->state == SDYING || p->state == SZOMB) {
        return 0;
    }

    /* Init Protection: Block SIGKILL/SIGTERM/SIGSTOP to PID 1 */
    if (p->pid == 1 && (sig == SIGKILL || sig == SIGTERM || sig == SIGSTOP)) {
        // Allow delivery if explicit handler is installed (not SIG_DFL)
        // Note: SIGKILL/SIGSTOP cannot usually be caught, so they remain blocked here
        // unless sys_sigaction laws change. SIGTERM can be caught.
        if (p->sig_actions[sig - 1].sa_handler == SIG_DFL) {
            return 0;
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
                sigchld_notify(p->p_parent, CLD_CONTINUED, p->pid, p->uid, SIGCONT);
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
            int awaited = 0;
            FOREACH_THREAD(t) {
                if (t->proc != p) continue;
                if (!(t->sig_mask & m)) { deliverable_now = 1; }
                /* A thread synchronously waiting for this signal in
                 * sigwait/sigtimedwait must receive it even though the
                 * disposition is ignore — do not discard it out from under
                 * the waiter. */
                if (t->sig_wait_mask & m) { awaited = 1; }
            }
            if (deliverable_now && !awaited)
                return 0;
        }
    }

    /* Real-time signal: enqueue a distinct instance so it does not
     * coalesce into the single sig_pending bit.  Skip when this signo
     * already carries a pending timer notification (sig_timer_pend set by
     * time.c before calling psignal) — the timer path owns that delivery
     * via sig_qval/SI_TIMER and must not be shadowed by a queued node.
     * The pending bit(s) set by the loop below stay set until the queue
     * drains (see signal_handle_pending), which is what makes each queued
     * instance deliver exactly once. */
    if (SIG_IS_RT(sig) && !(p->sig_timer_pend & sigmask(sig))) {
        union sigval v = valp ? *valp : (union sigval){ .sival_int = 0 };
        int enq = rtsig_enqueue(p, sig, si_code, v);
        if (enq != 0 && si_code == SI_QUEUE) {
            /* sigqueue(2) overflow: report EAGAIN and do NOT post — the
             * caller (sys_sigqueue) turns this into the userspace error. */
            return -EAGAIN;
        }
        /* enq != 0 on the kill/SI_USER path: queue full, fall through and
         * post the pending bit anyway (best effort). */
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
        
        /* Wake interruptibly-sleeping threads.  Also wake a thread that is
         * synchronously waiting for this signal in sigwait/sigtimedwait even
         * when it has the signal masked (sig_wait_mask) — otherwise a timer or
         * kill() delivering a masked-but-awaited signal would leave the waiter
         * asleep until its timeout. */
        if (t->state == THREAD_BLOCKED &&
            (unmasked || (t->sig_wait_mask & sig_mask)) &&
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
    return 0;
}

/*
 * psignal - Send signal to a process (si_code SI_USER, no sigqueue payload).
 * Thin wrapper over psignal_info(); this is the process-directed entry point
 * used everywhere in the kernel (kill, TTY, timers, child-exit, ...).
 */
void psignal(process_t *p, int sig) {
    (void)psignal_info(p, sig, SI_USER, NULL);
}

/*
 * sigchld_notify - post SIGCHLD to a parent carrying child-status siginfo.
 *
 * SIGCHLD is a standard (coalescing) signal, so like the sigqueue payload it
 * cannot carry per-instance siginfo through the pending bitmask alone.  We
 * stash the CLD_* code plus the child's pid/uid/status on the PARENT process;
 * populate_siginfo() reads it back when it builds the SA_SIGINFO frame and then
 * clears sigchld_pend.  Without this, a stop/continue-generated SIGCHLD reached
 * the parent's handler with si_code == SI_USER, so job-control monitors (OPTS
 * signals/sigaction/10-1) never saw CLD_STOPPED / CLD_CONTINUED.
 */
void sigchld_notify(process_t *parent, int code, int cpid,
                    unsigned int cuid, int status) {
    if (!parent) return;
    parent->sigchld_code   = code;
    parent->sigchld_cpid   = cpid;
    parent->sigchld_cuid   = cuid;
    parent->sigchld_status = status;
    parent->sigchld_pend   = 1;
    psignal(parent, SIGCHLD);
}

// Send signal to process group
void pgsignal(int pgrp_id, int sig) {
    if (pgrp_id <= 0 || sig <= 0 || sig >= NSIG) return;
    
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
    if (!p || sig <= 0 || sig >= NSIG) return;
    
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
    if (!p || sig <= 0 || sig >= NSIG) return;
    
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
    if (sig < 0 || sig >= NSIG) return -EINVAL;

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
 *
 * A real-time signal ([SIGRTMIN,SIGRTMAX]) truly QUEUES: every call enqueues
 * a distinct instance on the target's rtsig_q[], delivered in order, each
 * carrying its own si_value; the call returns EAGAIN if the queue is full
 * (RTSIG_QUEUE_MAX).  A standard signal cannot queue (32-bit bitmask pending
 * set), so its payload is stored one-per-signal-number and the most recent
 * value wins if it is queued repeatedly before delivery.  sig == 0 performs
 * only the permission/existence check.
 */
int sys_sigqueue(int pid, int sig, uintptr_t sival) {
    if (sig < 0 || sig >= NSIG) return -EINVAL;
    /* POSIX sigqueue() addresses a single process by (positive) pid. */
    if (pid <= 0) return -EINVAL;

    process_t *target = proc_find(pid);
    if (!target) return -ESRCH;
    if (!signal_can_send(current_process, target)) return -EPERM;

    if (sig != 0) {
        union sigval v;
        v.sival_ptr = (void *)sival;
        if (SIG_IS_RT(sig)) {
            /* Real queuing: psignal_info enqueues the instance (SI_QUEUE +
             * payload) and posts it, or returns -EAGAIN if the queue is
             * full — propagate that to userspace unchanged. */
            return psignal_info(target, sig, SI_QUEUE, &v);
        }
        /* Standard signal: last-value-wins slot + bitmask, as before. */
        target->sig_qval[sig - 1] = v;
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

    /* Reset the per-thread RT-delivery scratch.  It is set below only when
     * we actually dequeue a queued RT instance, and is read by the arch
     * populate_siginfo() during this same delivery pass. */
    current_thread->rtsig_deliver_active = 0;

    uint32_t pending = current_thread->sig_pending & ~current_thread->sig_mask;
    if (pending == 0) return;

    // Find the first pending signal (lowest number wins)
    int sig = 0;
    for (int i = 0; i < NSIG; i++) {
        if (pending & (1 << i)) {
            sig = i + 1;
            break;
        }
    }

    if (sig == 0) return;

    /*
     * Clear the pending state for the selected signal.
     *
     * A real-time signal ([SIGRTMIN,SIGRTMAX]) that is NOT a pending timer
     * notification is backed by the process rtsig_q[]: dequeue exactly ONE
     * instance and stash its si_code/si_value for populate_siginfo.  Each
     * delivering thread clears only its OWN pending bit, and only when it
     * takes the LAST queued instance (rt_more == 0); while instances remain
     * the bit stays set so this thread's next return-to-userspace delivers
     * the next one — one instance per handler invocation, no coalescing.
     *
     * The take-last-instance -> clear-bit decision is made INSIDE
     * rtsig_dequeue under rtsig_lock, and rtsig_enqueue sets the pending bit
     * under the same lock, so the two are serialized: an enqueue can no longer
     * land between "took the last instance" and "cleared the bit" and strand
     * the new instance.  A sibling thread left holding a stale bit after the
     * queue drains hits the empty-dequeue path (which clears its own bit under
     * the lock, a no-op delivery); that path also absorbs a bit a timer set on
     * all threads once the timer notification has been consumed.
     */
    if (SIG_IS_RT(sig) && !(current_process->sig_timer_pend & sigmask(sig))) {
        int rt_code = SI_USER, rt_more = 0;
        union sigval rt_val = { .sival_int = 0 };
        int rt_pid = 0; uint32_t rt_uid = 0;
        /* rtsig_dequeue clears this thread's pending bit UNDER rtsig_lock iff
         * this was the last instance — atomically with the remaining-scan and
         * serialized against a concurrent rtsig_enqueue that sets the bit
         * under the same lock.  That closes the lost/stranded-signal race an
         * outside-the-lock clear used to have.  While instances remain the
         * bit stays set so the next return-to-userspace delivers the next
         * one — one instance per handler invocation, no coalescing. */
        if (!rtsig_dequeue(current_process, sig, &rt_code, &rt_val, &rt_more,
                           current_thread, &rt_pid, &rt_uid)) {
            /* Nothing queued for this signo — the bit was cleared under the
             * lock by rtsig_dequeue (absorbs a stale bit left by a sibling
             * drain or a consumed timer notification).  Stop. */
            return;
        }
        current_thread->rtsig_deliver_active = 1;
        current_thread->rtsig_deliver_code = rt_code;
        current_thread->rtsig_deliver_value = rt_val;
        current_thread->rtsig_deliver_pid = rt_pid;
        current_thread->rtsig_deliver_uid = rt_uid;
    } else {
        // Standard signal (or timer notification): clear this thread's bit.
        __sync_fetch_and_and(&current_thread->sig_pending, ~sigmask(sig));
    }

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
                 sigchld_notify(current_process->p_parent, CLD_STOPPED,
                                current_process->pid, current_process->uid, sig);
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
        
        /*
         * All remaining signals: consult the sigprop[] default-action
         * table (SIGSEGV/ILL/FPE/BUS/TRAP and SIGINT/SIGTERM were handled
         * explicitly above; SIGSTOP-family just above; SIGKILL earlier).
         * PROP_KILL terminates the process — sigexit() adds a core dump
         * when PROP_CORE is set and dumping is permitted.  Everything else
         * (PROP_IGNORE: SIGCHLD, SIGCONT-when-not-stopped, SIGURG,
         * SIGWINCH) is ignored.
         *
         * Without this, every signal whose default action is "terminate"
         * but which is NOT in the hardcoded list above — SIGABRT, SIGHUP,
         * SIGQUIT, SIGUSR1, SIGUSR2, SIGPIPE, SIGALRM, SIGXCPU, SIGXFSZ,
         * SIGVTALRM, SIGPROF, SIGSYS — fell through to the "ignore" path
         * and was silently dropped.  So e.g. kill(child, SIGABRT) never
         * terminated a child with the default disposition (OPTS
         * mq_timedreceive/8-1: the parent could not abort a sleeping
         * child).
         */
        if (sigprop[sig] & PROP_KILL) {
            /* PID 1 (init) must survive every default-action terminate
             * signal, or the whole system halts with no init.  This mirrors
             * the SIGKILL/SIGTERM guard in psignal_info() and the explicit
             * SIGINT/SIGTERM block above; extend it across the entire
             * sigprop[] terminate path (SIGHUP/QUIT/ABRT/USR1/USR2/PIPE/
             * ALRM/XCPU/XFSZ/VTALRM/PROF/SYS all reach here now). */
            if (current_process->pid == 1) {
                signal_clear_trap_context(current_thread, sig);
                return;
            }
            signal_clear_trap_context(current_thread, sig);
            sigexit(current_process, sig);
            return;
        }

        // Ignore by default (SIGCHLD, SIGCONT-if-not-stopped, SIGURG, SIGWINCH)
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
