#include <sys/signal.h>
#include <sys/proc.h>
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
    if (oact) *oact = current_process->sig_actions[sig - 1];
    if (act) current_process->sig_actions[sig - 1] = *act;
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
    if (set) *set = current_thread->sig_pending;
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

int sys_kill(int pid, int sig) {
    if (sig < 0 || sig > NSIG) return -1; // EINVAL

    // Handle PID > 0: Send to specific process
    if (pid > 0) {
        // Init Protection
        if (pid == 1 && (sig == SIGKILL || sig == SIGTERM || sig == SIGSTOP)) {
            return -1; // EPERM
        }

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

        // Deliver signal to threads of the target
        int found_threads = 0;
        for (int i = 0; i < MAX_THREADS; i++) {
            if (threads[i].tid != -1 && threads[i].proc == target) {
                // SIGCONT Special handling: Wake up stopped threads
                if (sig == SIGCONT) {
                    if (threads[i].state == THREAD_STOPPED) {
                        threads[i].state = THREAD_READY;
                        found_threads++;
                    }
                    // Continue to deliver SIGCONT to pending as well? 
                    // Usually yes, so it can be caught.
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
        
        // If no threads found (zombie process?), maybe allow?
        // But the process exists.
        return 0;
    }
    else if (pid == 0) {
        // Send to current process group
        if (!current_process) return -1;
        return signal_send_group(current_process->pgrp, sig);
    }
    else if (pid == -1) {
        // Send to all processes (except Init)
        // Requires privilege check in real OS
        for (int i = 0; i < MAX_PROCS; i++) {
            if (processes[i].pid > 1) {
                 // Recursively call for single PID (simplifies logic)
                 sys_kill(processes[i].pid, sig);
            }
        }
        return 0;
    }
    else {
        // pid < -1: Send to process group -pid
        return signal_send_group(-pid, sig);
    }

    return -1;
}

int signal_send_group(int pgrp, int sig) {
    if (sig < 0 || sig > NSIG) return -1;
    if (pgrp <= 0) return -1;

    extern process_t processes[];
    int count = 0;

    for (int i = 0; i < MAX_PROCS; i++) {
        if (processes[i].pid != -1 && processes[i].pgrp == pgrp) {
            sys_kill(processes[i].pid, sig);
            count++;
        }
    }
    return (count > 0) ? 0 : -1;
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

    // Deliver signal via architecture-specific sendsig
    extern void sendsig(sig_t handler, int sig, uint32_t mask, registers_t *regs);
    sendsig(act->sa_handler, sig, current_thread->sig_mask, regs);
}
