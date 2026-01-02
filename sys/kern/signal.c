#include <sys/signal.h>
#include <sys/proc.h>
#include "sched.h"
#include "../arch/i386/idt.h"
#include "../drivers/video/vga.h"
#include "panic.h"
#include <stddef.h>

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
    if (sig < 0 || sig > NSIG) return -1;
    
    if (pid == 1 && (sig == SIGKILL || sig == SIGTERM || sig == SIGSTOP)) {
        return -1; // Operation not permitted on init
    }

    process_t *target = NULL;
    if (current_process && pid == current_process->pid) target = current_process;
    else {
        // Search for process by PID
        extern process_t processes[];
        for (int i = 0; i < 16; i++) {
            if (processes[i].pid == pid) {
                target = &processes[i];
                break;
            }
        }
    }

    if (!target) return -1;

    extern thread_t threads[];
    for (int i = 0; i < 64; i++) {
        if (threads[i].proc == target && threads[i].tid != -1) {
            threads[i].sig_pending |= sigmask(sig);
            // If thread is sleeping, wake it up (simplified)
            if (threads[i].state == THREAD_BLOCKED) {
                // If it was in sigsuspend, the wait_chan is &sig_pending
                sched_wakeup(&threads[i].sig_pending);
                // Also wake from generic sleep? 
                // In real OS, only EINTR-able sleeps are woken.
            }
            break;
        }
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

    // Clear pending bit
    current_thread->sig_pending &= ~sigmask(sig);

    struct sigaction *act = &current_process->sig_actions[sig - 1];

    if (act->sa_handler == SIG_IGN) {
        return;
    } else if (act->sa_handler == SIG_DFL) {
        // Default actions
        if (sig == SIGKILL || sig == SIGINT || sig == SIGTERM || sig == SIGSEGV) {
            vga_write("Process terminated by signal\n", 29);
            // In a real OS, call sys_exit
            panic("Process signal termination");
        }
        return;
    }

    // Deliver signal: Set up user stack frame
    // 1. Push current registers onto user stack
    // 2. Push signal number
    // 3. Push return address (trampoline)
    // 4. Set EIP to handler
    
    uint32_t esp = regs->useresp;
    
    // Push registers_t (simplified)
    esp -= sizeof(registers_t);
    // Note: We need to be careful about mapping here. 
    // For now, assume user stack is accessible.
    // memcpy((void*)esp, regs, sizeof(registers_t));
    
    // For this prototype, we'll just log and panic if a handler is set, 
    // as full user-mode signal delivery is complex.
    vga_write("Signal handler delivery not fully implemented in prototype\n", 51);
    panic("Signal handler delivery");
}
