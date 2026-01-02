#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include "../../../sys/sys/signal.h"
#include "../../../sys/sys/proc.h"
#include "../../../sys/arch/i386/idt.h"

extern int sys_sigaction(int sig, const struct sigaction *act, struct sigaction *oact);
extern int sys_sigprocmask(int how, const uint32_t *set, uint32_t *oset);
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
