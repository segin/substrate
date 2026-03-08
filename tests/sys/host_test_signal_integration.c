#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <sys/signal.h>
#include <sys/proc.h>
#include <sys/session.h>
#include <arch/i386/idt.h>
#include <pm/pm.h>
#include <kern/sched.h>
#include <exec/perso/personality.h>

thread_t threads[MAX_THREADS];
process_t processes[MAX_PROCS];
process_t *current_process;
thread_t *current_thread;
mutex_t proctree_lock;

static int proc_exit_called;
static int proc_exit_status;

void mutex_init(mutex_t *m, const char *name) { (void)m; (void)name; }
void mutex_lock(mutex_t *m) { (void)m; }
void mutex_unlock(mutex_t *m) { (void)m; }
void sched_yield(void) {}
void sched_sleep(void *chan) { (void)chan; }
void sched_wakeup(void *chan) { (void)chan; }
int sleepq_remove_thread(thread_t *t) { (void)t; return 0; }
void kprint(const char *msg) { (void)msg; }
void panic(const char *msg) { (void)msg; assert(!"panic"); }
uint64_t get_ticks(void) { return 0; }
uint32_t get_hz(void) { return 128; }
int copyin(const void *src, void *dst, size_t size) { memcpy(dst, src, size); return 0; }
int copyout(const void *src, void *dst, size_t size) { memcpy(dst, src, size); return 0; }
const uint8_t sigprop[NSIG] = {0};
void sendsig(sig_t handler, int sig, uint32_t mask, uint32_t flags, registers_t *regs) {
    (void)handler; (void)sig; (void)mask; (void)flags; (void)regs;
}
struct personality *perso_lookup(int id) { (void)id; return NULL; }
const char *perso_name(int id) { (void)id; return "test"; }
struct pgrp *pgrp_find(int pgid) {
    for (int i = 0; i < MAX_PROCS; i++) {
        if (processes[i].pid > 0 &&
            processes[i].p_pgrp &&
            processes[i].p_pgrp->pg_id == pgid) {
            return processes[i].p_pgrp;
        }
    }
    return NULL;
}
void pgrp_signal(struct pgrp *pgrp, int sig) {
    process_t *member = pgrp ? pgrp->pg_members : NULL;
    while (member) {
        psignal(member, sig);
        member = member->p_pgrp_link;
    }
}
int pgrp_is_orphaned(struct pgrp *pgrp) { (void)pgrp; return 0; }
void proc_exit(int status) { proc_exit_called = 1; proc_exit_status = status; }

#include "../../sys/kern/signal.c"

static void reset_env(void) {
    memset(threads, 0, sizeof(threads));
    memset(processes, 0, sizeof(processes));
    proc_exit_called = 0;
    proc_exit_status = 0;
    for (int i = 0; i < MAX_PROCS; i++) {
        processes[i].pid = -1;
    }
    for (int i = 0; i < MAX_THREADS; i++) {
        threads[i].tid = -1;
    }

    current_process = NULL;
    current_thread = NULL;
}

static void test_sigint_kills_target_child(void) {
    registers_t regs;
    process_t *parent;
    process_t *child;
    thread_t *child_thread;

    reset_env();
    memset(&regs, 0, sizeof(regs));

    parent = &processes[0];
    child = &processes[1];
    child_thread = &threads[0];

    parent->pid = 10;
    parent->state = SRUN;

    child->pid = 11;
    child->ppid = parent->pid;
    child->p_parent = parent;
    child->state = SRUN;

    child_thread->tid = 101;
    child_thread->proc = child;
    child_thread->state = THREAD_RUNNING;
    child_thread->sig_mask = 0;
    child_thread->sig_pending = 0;

    current_process = parent;
    current_thread = &threads[1];
    current_thread->tid = 100;
    current_thread->proc = parent;
    current_thread->state = THREAD_RUNNING;

    assert(sys_kill(child->pid, SIGINT) == 0);
    assert(child_thread->sig_pending & sigmask(SIGINT));

    current_process = child;
    current_thread = child_thread;
    signal_handle_pending(&regs);

    assert(proc_exit_called == 1);
    assert(proc_exit_status == SIGINT);
    assert(child->exit_code == SIGINT);
}

static void test_sys_kill_permission_and_group_routing(void) {
    process_t *caller;
    process_t *peer;
    process_t *other;
    process_t *init;
    thread_t *caller_thread;
    thread_t *peer_thread;
    thread_t *other_thread;
    thread_t *init_thread;
    struct session sess;
    struct pgrp grp;

    reset_env();
    memset(&sess, 0, sizeof(sess));
    memset(&grp, 0, sizeof(grp));

    caller = &processes[0];
    peer = &processes[1];
    other = &processes[2];
    init = &processes[3];

    caller_thread = &threads[0];
    peer_thread = &threads[1];
    other_thread = &threads[2];
    init_thread = &threads[3];

    caller->pid = 20;
    caller->uid = 1000;
    caller->euid = 1000;
    caller->state = SRUN;
    caller_thread->tid = 200;
    caller_thread->proc = caller;
    caller_thread->state = THREAD_RUNNING;

    peer->pid = 21;
    peer->uid = 1000;
    peer->euid = 1000;
    peer->state = SRUN;
    peer_thread->tid = 201;
    peer_thread->proc = peer;
    peer_thread->state = THREAD_READY;

    other->pid = 22;
    other->uid = 2000;
    other->euid = 2000;
    other->state = SRUN;
    other_thread->tid = 202;
    other_thread->proc = other;
    other_thread->state = THREAD_READY;

    init->pid = 1;
    init->uid = 0;
    init->euid = 0;
    init->state = SRUN;
    init_thread->tid = 203;
    init_thread->proc = init;
    init_thread->state = THREAD_READY;

    sess.s_sid = caller->pid;
    sess.s_leader = caller;
    grp.pg_id = caller->pid;
    grp.pg_session = &sess;
    grp.pg_members = caller;
    caller->p_pgrp = &grp;
    caller->p_pgrp_link = peer;
    peer->p_pgrp = &grp;
    peer->p_pgrp_link = NULL;

    current_process = caller;
    current_thread = caller_thread;

    assert(sys_kill(peer->pid, 0) == 0);
    assert(peer_thread->sig_pending == 0);

    assert(sys_kill(other->pid, 0) == -EPERM);
    assert(other_thread->sig_pending == 0);

    assert(sys_kill(0, SIGUSR1) == 0);
    assert(caller_thread->sig_pending & sigmask(SIGUSR1));
    assert(peer_thread->sig_pending & sigmask(SIGUSR1));
    assert((other_thread->sig_pending & sigmask(SIGUSR1)) == 0);

    caller_thread->sig_pending = 0;
    peer_thread->sig_pending = 0;

    assert(sys_kill(-grp.pg_id, SIGUSR2) == 0);
    assert(caller_thread->sig_pending & sigmask(SIGUSR2));
    assert(peer_thread->sig_pending & sigmask(SIGUSR2));

    caller_thread->sig_pending = 0;
    peer_thread->sig_pending = 0;
    other_thread->sig_pending = 0;
    init_thread->sig_pending = 0;

    caller->uid = 0;
    caller->euid = 0;
    assert(sys_kill(-1, SIGWINCH) == 0);
    assert(caller_thread->sig_pending & sigmask(SIGWINCH));
    assert(peer_thread->sig_pending & sigmask(SIGWINCH));
    assert(other_thread->sig_pending & sigmask(SIGWINCH));
    assert((init_thread->sig_pending & sigmask(SIGWINCH)) == 0);
}

static void test_psignal_interrupts_generic_sleep(void) {
    process_t *proc;
    thread_t *thread;
    int wait_chan = 0;

    reset_env();

    proc = &processes[0];
    thread = &threads[0];

    proc->pid = 30;
    proc->uid = 1000;
    proc->euid = 1000;
    proc->state = SRUN;

    thread->tid = 300;
    thread->proc = proc;
    thread->state = THREAD_BLOCKED;
    thread->flags = THREAD_F_INTERRUPTIBLE;
    thread->wait_chan = &wait_chan;
    thread->sleep_status = 0;

    current_process = proc;
    current_thread = thread;

    psignal(proc, SIGUSR1);

    assert(thread->state == THREAD_READY);
    assert(thread->wait_chan == NULL);
    assert(thread->sleep_status == -EINTR);
    assert(thread->sig_pending & sigmask(SIGUSR1));
}

static void test_sigstop_stops_entire_process(void) {
    registers_t regs;
    process_t *parent;
    process_t *proc;
    thread_t *leader;
    thread_t *peer;

    reset_env();
    memset(&regs, 0, sizeof(regs));
    regs.cs = 0x1b;

    parent = &processes[0];
    proc = &processes[1];
    leader = &threads[0];
    peer = &threads[1];

    parent->pid = 40;
    parent->state = SRUN;

    proc->pid = 41;
    proc->p_parent = parent;
    proc->state = SRUN;

    leader->tid = 400;
    leader->proc = proc;
    leader->state = THREAD_RUNNING;
    leader->sig_pending = sigmask(SIGSTOP);

    peer->tid = 401;
    peer->proc = proc;
    peer->state = THREAD_READY;

    current_process = proc;
    current_thread = leader;

    signal_handle_pending(&regs);

    assert(proc->state == SSTOP);
    assert(leader->state == THREAD_STOPPED);
    assert(peer->state == THREAD_STOPPED);
    assert(leader->wait_reason != NULL);
}

static void test_sigkill_resumes_stopped_threads_before_exit(void) {
    registers_t regs;
    process_t *proc;
    thread_t *leader;
    thread_t *peer;

    reset_env();
    memset(&regs, 0, sizeof(regs));
    regs.cs = 0x1b;

    proc = &processes[0];
    leader = &threads[0];
    peer = &threads[1];

    proc->pid = 50;
    proc->state = SSTOP;

    leader->tid = 500;
    leader->proc = proc;
    leader->state = THREAD_STOPPED;
    leader->sig_pending = sigmask(SIGKILL);

    peer->tid = 501;
    peer->proc = proc;
    peer->state = THREAD_STOPPED;

    current_process = proc;
    current_thread = leader;

    signal_handle_pending(&regs);

    assert(proc_exit_called == 1);
    assert(proc_exit_status == SIGKILL);
    assert(proc->state == SRUN);
    assert(leader->state == THREAD_READY);
    assert(peer->state == THREAD_READY);
}

int main(void) {
    test_sigint_kills_target_child();
    test_sys_kill_permission_and_group_routing();
    test_psignal_interrupts_generic_sleep();
    test_sigstop_stops_entire_process();
    test_sigkill_resumes_stopped_threads_before_exit();
    puts("host_test_signal_integration: PASS");
    return 0;
}
