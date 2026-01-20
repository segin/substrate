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

// Send signal to valid process
void psignal(process_t *p, int sig) {
    if (!p || sig <= 0 || sig > NSIG) return;
    
    // Init Protection
    if (p->pid == 1 && (sig == SIGKILL || sig == SIGTERM || sig == SIGSTOP)) {
        return;
    }

    // Deliver signal to threads of the target
    int found_threads = 0;
    
    // TODO: Improve thread lookup (process should have thread list)
    // For now, scan global thread list
    extern thread_t threads[MAX_THREADS];
    
    for (int i = 0; i < MAX_THREADS; i++) {
        if (threads[i].tid != -1 && threads[i].proc == p) {
            // SIGCONT Special handling: Wake up stopped threads
            if (sig == SIGCONT) {
                if (threads[i].state == THREAD_STOPPED) {
                    threads[i].state = THREAD_READY;
                    found_threads++;
                }
            }

            threads[i].sig_pending |= sigmask(sig);
            
            // Wake up if sleeping interruptibly
            if (threads[i].state == THREAD_BLOCKED) {
                // We wake on the pending mask address which sigsuspend/sleep uses
                sched_wakeup(&threads[i].sig_pending);
            }
            found_threads++;
        }
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

// Send synchronous trap signal (e.g. from exception)
void trapsignal(process_t *p, int sig, int code) {
    (void)code; // Unused for now
    // TODO: Pass code via siginfo (SA_SIGINFO)
    psignal(p, sig);
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

    struct sigaction *act = &current_process->sig_actions[sig - 1];

    if (act->sa_handler == SIG_IGN) {
        return;
    } else if (act->sa_handler == SIG_DFL) {
        // Default actions
        if (sig == SIGKILL || sig == SIGINT || sig == SIGTERM || sig == SIGSEGV) {
            kprint("Process terminated by signal\n");
            // In a real OS, call sys_exit
            panic("Process signal termination");
        }
        
        // Job Control Stops
        if (sig == SIGSTOP || sig == SIGTSTP || sig == SIGTTIN || sig == SIGTTOU) {
             // Stop the thread
             // kprint("Process stopped by signal\n");
             current_thread->state = THREAD_STOPPED;
             current_thread->wait_reason = "Signal";
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
