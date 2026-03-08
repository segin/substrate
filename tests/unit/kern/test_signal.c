#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <sys/signal.h>
#include <sys/proc.h>
#include <sys/session.h>
#include <arch/i386/idt.h>
#include <pm/pm.h>
#include <kern/sched.h>
#include <exec/perso/personality.h>

static thread_t *alloc_test_thread(process_t *proc, thread_state_t state,
                                   uint32_t mask, uint32_t flags) {
    thread_t *t = sched_alloc_thread(proc);
    if (!t) return NULL;
    t->proc = proc;
    t->state = state;
    t->sig_mask = mask;
    t->sig_pending = 0;
    t->flags = flags;
    t->wait_chan = NULL;
    proc->state = SRUN;
    return t;
}

static void install_current(process_t *proc, thread_t *thread) {
    current_process = proc;
    current_thread = thread;
    thread->proc = proc;
    thread->state = THREAD_RUNNING;
    proc->state = SRUN;
}

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

bool test_signal_init_protection(void) {
    sched_init();

    process_t *init = proc_create(PERS_NATIVE);
    if (!init) return false;
    thread_t *init_thread = alloc_test_thread(init, THREAD_READY, 0, 0);
    if (!init_thread) return false;

    psignal(init, SIGKILL);
    psignal(init, SIGTERM);
    psignal(init, SIGSTOP);
    if (init_thread->sig_pending != 0) return false;

    struct sigaction act = {
        .sa_handler = (sig_t)0x1234,
        .sa_mask = 0,
        .sa_flags = 0,
    };
    init->sig_actions[SIGTERM - 1] = act;

    psignal(init, SIGTERM);
    if (!(init_thread->sig_pending & sigmask(SIGTERM))) return false;

    return true;
}

bool test_signal_kill_routing(void) {
    sched_init();

    process_t *init = proc_create(PERS_NATIVE);
    if (!init) return false;
    thread_t *init_thread = alloc_test_thread(init, THREAD_READY, 0, 0);
    if (!init_thread) return false;

    process_t *caller = proc_create(PERS_NATIVE);
    process_t *peer = proc_create(PERS_NATIVE);
    process_t *other = proc_create(PERS_NATIVE);
    if (!caller || !peer || !other) return false;

    thread_t *caller_thread = alloc_test_thread(caller, THREAD_READY, 0, 0);
    thread_t *peer_thread = alloc_test_thread(peer, THREAD_READY, 0, 0);
    thread_t *other_thread = alloc_test_thread(other, THREAD_READY, 0, 0);
    if (!caller_thread || !peer_thread || !other_thread) return false;

    install_current(caller, caller_thread);

    struct session sess;
    struct pgrp grp;
    memset(&sess, 0, sizeof(sess));
    memset(&grp, 0, sizeof(grp));
    sess.s_sid = caller->pid;
    sess.s_leader = caller;
    grp.pg_id = caller->pid;
    grp.pg_session = &sess;
    grp.pg_members = caller;
    caller->p_pgrp = &grp;
    caller->p_pgrp_link = peer;
    peer->p_pgrp = &grp;
    peer->p_pgrp_link = NULL;

    if (sys_kill(peer->pid, SIGUSR1) != 0) return false;
    if (!(peer_thread->sig_pending & sigmask(SIGUSR1))) return false;
    if (other_thread->sig_pending != 0) return false;

    if (sys_kill(-grp.pg_id, SIGUSR2) != 0) return false;
    if (!(caller_thread->sig_pending & sigmask(SIGUSR2))) return false;
    if (!(peer_thread->sig_pending & sigmask(SIGUSR2))) return false;
    if (other_thread->sig_pending & sigmask(SIGUSR2)) return false;

    if (sys_kill(-1, SIGWINCH) != 0) return false;
    if (init_thread->sig_pending & sigmask(SIGWINCH)) return false;
    if (!(caller_thread->sig_pending & sigmask(SIGWINCH))) return false;
    if (!(peer_thread->sig_pending & sigmask(SIGWINCH))) return false;
    if (!(other_thread->sig_pending & sigmask(SIGWINCH))) return false;

    return true;
}

bool test_psignal_thread_selection(void) {
    sched_init();

    process_t *proc = proc_create(PERS_NATIVE);
    if (!proc) return false;

    thread_t *masked = alloc_test_thread(proc, THREAD_BLOCKED, sigmask(SIGUSR1),
                                         THREAD_F_INTERRUPTIBLE);
    thread_t *interruptible = alloc_test_thread(proc, THREAD_BLOCKED, 0,
                                                THREAD_F_INTERRUPTIBLE);
    thread_t *noninterruptible = alloc_test_thread(proc, THREAD_BLOCKED, 0, 0);
    if (!masked || !interruptible || !noninterruptible) return false;

    masked->wait_chan = &masked->sig_pending;
    interruptible->wait_chan = &interruptible->sig_pending;
    noninterruptible->wait_chan = &noninterruptible->sig_pending;

    psignal(proc, SIGUSR1);

    if (!(masked->sig_pending & sigmask(SIGUSR1))) return false;
    if (!(interruptible->sig_pending & sigmask(SIGUSR1))) return false;
    if (!(noninterruptible->sig_pending & sigmask(SIGUSR1))) return false;

    if (masked->state != THREAD_BLOCKED) return false;
    if (interruptible->state != THREAD_READY) return false;
    if (interruptible->wait_chan != NULL) return false;
    if (noninterruptible->state != THREAD_BLOCKED) return false;

    return true;
}

bool test_signal_sigcont_clears_stops(void) {
    sched_init();

    process_t *parent = proc_create(PERS_NATIVE);
    process_t *child = proc_create(PERS_NATIVE);
    if (!parent || !child) return false;

    thread_t *parent_thread = alloc_test_thread(parent, THREAD_READY, 0, 0);
    thread_t *child_a = alloc_test_thread(child, THREAD_STOPPED, 0, 0);
    thread_t *child_b = alloc_test_thread(child, THREAD_STOPPED, 0, 0);
    if (!parent_thread || !child_a || !child_b) return false;

    child->p_parent = parent;
    child->state = SSTOP;
    child_a->sig_pending = sigmask(SIGTSTP) | sigmask(SIGTTIN);
    child_b->sig_pending = sigmask(SIGTTOU) | sigmask(SIGSTOP);

    psignal(child, SIGCONT);

    if (child->state != SRUN) return false;
    if (!(child->p_flag & P_CONTINUED)) return false;
    if (child_a->state != THREAD_READY) return false;
    if (child_b->state != THREAD_READY) return false;
    if (child_a->sig_pending & (sigmask(SIGSTOP) | sigmask(SIGTSTP) |
                                sigmask(SIGTTIN) | sigmask(SIGTTOU))) {
        return false;
    }
    if (child_b->sig_pending & (sigmask(SIGSTOP) | sigmask(SIGTSTP) |
                                sigmask(SIGTTIN) | sigmask(SIGTTOU))) {
        return false;
    }
    if (!(child_a->sig_pending & sigmask(SIGCONT))) return false;
    if (!(child_b->sig_pending & sigmask(SIGCONT))) return false;
    if (!(parent_thread->sig_pending & sigmask(SIGCHLD))) return false;

    return true;
}

bool test_trapsignal_records_thread_context(void) {
    sched_init();

    process_t *proc = proc_create(PERS_NATIVE);
    if (!proc) return false;
    thread_t *thread = alloc_test_thread(proc, THREAD_RUNNING, 0, 0);
    if (!thread) return false;

    install_current(proc, thread);
    trapsignal(proc, SIGSEGV, 0x2a);

    if (thread->trap_signo != SIGSEGV) return false;
    if (thread->trap_code != 0x2a) return false;
    if (!(thread->sig_pending & sigmask(SIGSEGV))) return false;

    return true;
}
