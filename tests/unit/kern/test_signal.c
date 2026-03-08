#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <sys/signal.h>
#include <sys/proc.h>
#include <arch/i386/idt.h>
extern int sys_kill(int pid, int sig);
extern void signal_handle_pending(registers_t *regs);
extern void sched_init(void);

bool test_signal_action(void) {
    sched_init();
    struct sigaction act, oact;
    act.sa_handler = (sig_t)0x1234;
    act.sa_mask = 0;
    act.sa_flags = 0;
    
    if (sys_sigaction(SIGINT, &act, &oact) != 0) return false;
    
    struct sigaction check;
    if (sys_sigaction(SIGINT, NULL, &check) != 0) return false;
    if (check.sa_handler != (sig_t)0x1234) return false;

    act.sa_handler = SIG_DFL;
    if (sys_sigaction(SIGINT, &act, &oact) != 0) return false;
    if (oact.sa_handler != (sig_t)0x1234) return false;
    if (sys_sigaction(SIGINT, NULL, &check) != 0) return false;
    if (check.sa_handler != SIG_DFL) return false;
    
    return true;
}

bool test_signal_mask(void) {
    sched_init();
    uint32_t set = sigmask(SIGINT);
    uint32_t oset;
    
    if (sys_sigprocmask(1, &set, &oset) != 0) return false; // SIG_BLOCK
    if (!(current_thread->sig_mask & sigmask(SIGINT))) return false;
    
    set = sigmask(SIGINT);
    if (sys_sigprocmask(2, &set, &oset) != 0) return false; // SIG_UNBLOCK
    if (current_thread->sig_mask & sigmask(SIGINT)) return false;

    set = sigmask(SIGTERM);
    if (sys_sigprocmask(3, &set, &oset) != 0) return false; // SIG_SETMASK
    if (current_thread->sig_mask != sigmask(SIGTERM)) return false;
    
    return true;
}

bool test_signal_delivery_default(void) {
    sched_init();
    current_process->pid = 100;
    current_thread->proc = current_process;
    
    // Send SIGTERM
    sys_kill(100, SIGTERM);
    
    if (!(current_thread->sig_pending & sigmask(SIGTERM))) return false;
    
    // We expect signal_handle_pending to panic/terminate for SIGTERM default action
    // But since it calls panic, we can't easily test it here without catching the panic.
    // So we just check pending for now.
    
    return true;
}

bool test_signal_pending_masked_filter(void) {
    sched_init();

    current_thread->sig_pending = sigmask(SIGINT) | sigmask(SIGTERM);
    current_thread->sig_mask = sigmask(SIGINT);

    uint32_t pending = 0;
    if (sys_sigpending(&pending) != 0) return false;
    if (pending != sigmask(SIGTERM)) return false;

    return true;
}

bool test_signal_uncatchable_invariants(void) {
    sched_init();

    struct sigaction act;
    act.sa_handler = (sig_t)0x1234;
    act.sa_mask = 0;
    act.sa_flags = 0;

    if (sys_sigaction(SIGKILL, &act, NULL) == 0) return false;
    if (sys_sigaction(SIGSTOP, &act, NULL) == 0) return false;

    uint32_t set = sigmask(SIGKILL) | sigmask(SIGSTOP) | sigmask(SIGINT);
    if (sys_sigprocmask(3, &set, NULL) != 0) return false;
    if (current_thread->sig_mask & sigmask(SIGKILL)) return false;
    if (current_thread->sig_mask & sigmask(SIGSTOP)) return false;
    if (!(current_thread->sig_mask & sigmask(SIGINT))) return false;

    return true;
}
