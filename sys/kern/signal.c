#include <sys/signal.h>
#include <sys/proc.h>
#include <sys/session.h>
#include "sched.h"
#include "../arch/i386/idt.h"
#include "console.h"
#include "panic.h"
#include <stddef.h>
#include "../pm/pm.h"

extern thread_t threads[MAX_THREADS];

// Signal System Calls
int sys_sigaction(int sig, const struct sigaction *act, struct sigaction *oact) {
    if (sig <= 0 || sig > NSIG || sig == SIGKILL || sig == SIGSTOP) return -1;
    
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

int sys_sigprocmask(int how, const uint32_t *set, uint32_t *oset) {
    if (oset) *oset = current_thread->sig_mask;
    if (set) {
        uint32_t new_set = *set;
        new_set &= ~(sigmask(SIGKILL) | sigmask(SIGSTOP));
        if (how == 1)      current_thread->sig_mask |= new_set;     // SIG_BLOCK
        else if (how == 2) current_thread->sig_mask &= ~new_set;    // SIG_UNBLOCK
        else if (how == 3) current_thread->sig_mask = new_set;      // SIG_SETMASK
    }
    return 0;
}

int sys_sigpending(uint32_t *set) {
    if (set) *set = current_thread->sig_pending & current_thread->sig_mask;
    return 0;
}

int sys_sigsuspend(const uint32_t *mask) {
    uint32_t old_mask = current_thread->sig_mask;
    if (mask) current_thread->sig_mask = *mask;
    
    // Sleep until a signal arrives
    while (!(current_thread->sig_pending & ~current_thread->sig_mask)) {
        sched_sleep(&current_thread->sig_pending);
    }
    
    current_thread->sig_mask = old_mask;
    return -1; // Always returns -1 (EINTR)
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
    if (!set || !sig) return 22; // EINVAL
    
    uint32_t wait_mask = *set;
    
    // Cannot wait for SIGKILL or SIGSTOP
    wait_mask &= ~(sigmask(SIGKILL) | sigmask(SIGSTOP));
    
    if (wait_mask == 0) return 22; // EINVAL - no valid signals
    
    // Block until a signal in wait_mask becomes pending
    while (1) {
        uint32_t deliverable = current_thread->sig_pending & wait_mask;
        
        if (deliverable) {
            // Find the first matching signal
            for (int i = 0; i < NSIG; i++) {
                if (deliverable & (1 << i)) {
                    int signal = i + 1;
                    // Remove from pending (consume it)
                    current_thread->sig_pending &= ~sigmask(signal);
                    // Return signal number
                    *sig = signal;
                    return 0; // Success
                }
            }
        }
        
        // No matching signal pending, sleep and retry
        sched_sleep(&current_thread->sig_pending);
        
        // Check if we were woken by a non-matching signal that's unmasked
        // (which would normally trigger handler delivery)
        uint32_t other_pending = current_thread->sig_pending & ~current_thread->sig_mask & ~wait_mask;
        if (other_pending) {
            // Let the normal signal delivery mechanism handle it
            // We return EINTR so the syscall can be restarted
            return 4; // EINTR
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
    if (!set) return -22; // EINVAL
    
    uint32_t wait_mask = *set;
    wait_mask &= ~(sigmask(SIGKILL) | sigmask(SIGSTOP));
    
    if (wait_mask == 0) return -22; // EINVAL
    
    // TODO: Implement proper timeout handling using kernel tick counter
    // For now, if timeout is NULL, wait indefinitely
    // If timeout is specified but we can't implement it, treat as no-wait
    (void)timeout; // Placeholder
    
    // Check for immediately available signal
    uint32_t deliverable = current_thread->sig_pending & wait_mask;
    
    if (!deliverable && timeout != NULL) {
        // Non-blocking check failed
        return -11; // EAGAIN
    }
    
    // Block until signal available (if no timeout)
    while (!deliverable) {
        sched_sleep(&current_thread->sig_pending);
        deliverable = current_thread->sig_pending & wait_mask;
        
        // Check for interruption by other signals
        uint32_t other_pending = current_thread->sig_pending & ~current_thread->sig_mask & ~wait_mask;
        if (other_pending) {
            return -4; // EINTR
        }
    }
    
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
    current_thread->sig_pending &= ~sigmask(signal);
    
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
    
    /* Init Protection: Block SIGKILL/SIGTERM/SIGSTOP to PID 1 */
    if (p->pid == 1 && (sig == SIGKILL || sig == SIGTERM || sig == SIGSTOP)) {
        return;
    }

    /* Handle SIGCONT: Resume stopped process */
    if (sig == SIGCONT) {
        if (p->state == SSTOP) {
            p->state = SRUN;
            /* Notify parent if not SA_NOCLDSTOP */
            if (p->p_parent && 
                !(p->p_parent->sig_actions[SIGCHLD-1].sa_flags & SA_NOCLDSTOP)) {
                psignal(p->p_parent, SIGCHLD);
            }
        }
    }

    /* Select best thread for delivery and set pending on all threads */
    extern thread_t threads[MAX_THREADS];
    uint32_t sig_mask = sigmask(sig);
    
    thread_t *best_thread = NULL;
    int best_priority = -1; // Higher is better
    
    for (int i = 0; i < MAX_THREADS; i++) {
        if (threads[i].tid == -1 || threads[i].proc != p) continue;
        
        thread_t *t = &threads[i];
        
        /* SIGCONT Special: Wake up stopped threads */
        if (sig == SIGCONT && t->state == THREAD_STOPPED) {
            t->state = THREAD_READY;
        }
        
        /* Set pending bit on ALL threads (signal is process-directed) */
        t->sig_pending |= sig_mask;
        
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
            } else if (t->state == THREAD_BLOCKED) {
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
        if (t->state == THREAD_BLOCKED && unmasked) {
            sched_wakeup(&t->sig_pending);
        }
    }
    
    /* If we found a best thread and it's blocked, wake it */
    if (best_thread && best_thread->state == THREAD_BLOCKED && 
        best_priority > 0) {
        sched_wakeup(&best_thread->sig_pending);
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
        /* Store trap info in thread's pending siginfo for later delivery */
        current_thread->trap_signo = sig;
        current_thread->trap_code = code;
        
        /* Set signal pending on this specific thread */
        current_thread->sig_pending |= sigmask(sig);
        
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
    
    /* Check if core dump is required */
    int do_core = (sigprop[sig] & SA_CORE) != 0;
    
    if (do_core) {
        /* Call coredump routine if available */
        extern int coredump(process_t *p); // May not be implemented yet
        // coredump(p); // Stub - uncomment when coredump is implemented
        (void)do_core; // Suppress unused warning for now
    }
    
    /* Set exit status to indicate signal termination:
     * POSIX encoding: signal number in bits 0-6, core dump in bit 7
     * This makes WIFSIGNALED(status) true and WTERMSIG return sig
     */
    int exit_status = sig & 0x7F; // Signal number in low 7 bits
    if (do_core) {
        exit_status |= 0x80; // Set core dump bit
    }
    
    /* Terminate the process */
    extern void proc_exit(int status);
    p->exit_code = exit_status;
    proc_exit(exit_status);
}

int sys_kill(int pid, int sig) {
    if (sig < 0 || sig > NSIG) return -1; // EINVAL

    // Handle PID > 0: Send to specific process
    if (pid > 0) {
        process_t *target = NULL;
        // Search process list
        for (int i = 0; i < MAX_PROCS; i++) {
            if (processes[i].pid == pid) {
                target = &processes[i];
                break;
            }
        }

        if (!target) return -1; // ESRCH
        if (sig == 0) return 0; // Existence check

        // Permission check
        if (current_process->uid != 0 && current_process->uid != target->uid) {
            return -1; // EPERM
        }

        psignal(target, sig);
        return 0;
    }
    else if (pid == 0) {
        // Send to current process group
        if (!current_process || !current_process->p_pgrp) return -1;
        pgsignal(current_process->p_pgrp->pg_id, sig);
        return 0;
    }
    else if (pid == -1) {
        // Send to all processes (except Init)
        for (int i = 0; i < MAX_PROCS; i++) {
            if (processes[i].pid > 1) {
                 psignal(&processes[i], sig);
            }
        }
        return 0;
    }
    else {
        // pid < -1: Send to process group -pid
        pgsignal(-pid, sig);
        return 0;
    }
}

int signal_send_group(int pgrp, int sig) {
    pgsignal(pgrp, sig);
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

    // Clear pending bit
    current_thread->sig_pending &= ~sigmask(sig);

    // Special Handling: SIGKILL always terminates immediately
    if (sig == SIGKILL) {
        // Wake all stopped threads before terminating (if any were stopped)
        extern void psignal(process_t *p, int sig);
        // Resending SIGCONT would wake them, but sigexit handles cleanup.
        // POSIX requires SIGKILL to work on stopped processes.
        if (current_process->state == SSTOP) {
            current_process->state = SRUN;
        }
        sigexit(current_process, SIGKILL);
        return; // Should not reach
    }

    struct sigaction *act = &current_process->sig_actions[sig - 1];

    if (act->sa_handler == SIG_IGN) {
        return;
    } else if (act->sa_handler == SIG_DFL) {
        // Default actions
        if (sig == SIGINT || sig == SIGTERM || sig == SIGSEGV || sig == SIGILL || sig == SIGFPE) {
            sigexit(current_process, sig);
            return;
        }
        
        // Job Control Stops
        if (sig == SIGSTOP || sig == SIGTSTP || sig == SIGTTIN || sig == SIGTTOU) {
             // Stop the thread and process
             // kprint("Process stopped by signal\n");
             current_thread->state = THREAD_STOPPED;
             current_process->state = SSTOP;
             current_thread->wait_reason = "Signal";
             
             // Notify parent if not SA_NOCLDSTOP
             if (current_process->p_parent && 
                 !(current_process->p_parent->sig_actions[SIGCHLD-1].sa_flags & SA_NOCLDSTOP)) {
                 psignal(current_process->p_parent, SIGCHLD);
             }
             
             sched_yield();
             return;
        }
        
        // Ignore others by default (like SIGCHLD, SIGCONT if not stopped)
        return;
    }

    // Capture handler config
    sig_t handler = act->sa_handler;
    uint32_t flags = act->sa_flags;
    uint32_t old_mask = current_thread->sig_mask;

    // SA_NODEFER: Block the signal itself unless requested not to
    uint32_t new_mask = old_mask | act->sa_mask;
    if (!(flags & SA_NODEFER)) {
        new_mask |= sigmask(sig);
    }
    current_thread->sig_mask = new_mask;

    // SA_RESETHAND: Reset to SIG_DFL
    if (flags & SA_RESETHAND) {
        struct sigaction dfl;
        dfl.sa_handler = SIG_DFL;
        dfl.sa_flags = 0;
        dfl.sa_mask = 0;
        sys_sigaction(sig, &dfl, NULL);
    }

    // Deliver signal via architecture-specific sendsig
    extern void sendsig(sig_t handler, int sig, uint32_t mask, uint32_t flags, registers_t *regs);
    
    /*
     * Check for SA_RESTART: If we're in a syscall that returned EINTR (-4) or 
     * similar restartable error, and SA_RESTART is set, restart the syscall
     * by rewinding EIP and restoring EAX.
     */
    if ((flags & SA_RESTART) && current_thread->in_syscall) {
        int32_t ret = (int32_t)regs->eax;
        if (ret == -4 || ret == -512) {  // EINTR or ERESTARTSYS
            /* 
             * Rewind EIP to point back to 'int $0x80' (2 bytes).
             * Restore original EAX (syscall number).
             * These changes are reflected in the sigcontext saved by sendsig,
             * so when the handler returns and sigreturn restores context,
             * the syscall will be re-executed.
             */
            regs->eip -= 2;
            regs->eax = current_thread->syscall_orig_eax;
        }
    }
    
    sendsig(handler, sig, old_mask, flags, regs);
}

int sys_sigaltstack(const stack_t *ss, stack_t *oss) {
    if (oss) {
        *oss = current_thread->sig_alt_stack;
    }
    
    if (ss) {
        if (ss->ss_flags & SS_DISABLE) {
            current_thread->sig_alt_stack.ss_flags = SS_DISABLE;
        } else {
            if (current_thread->sig_on_stack) return -1; // EPERM
            current_thread->sig_alt_stack = *ss;
            // Validate size?
            if (ss->ss_size < MINSIGSTKSZ) return -1; // ENOMEM
        }
    }
    return 0;
}
