#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <pm/pm.h>
#include <kern/sched.h>
#include <arch/i386/pmap.h>
#include <sys/signal.h>

static thread_t *forked_thread;
static process_t *forked_process;
fs_node_t *fs_root;
thread_t threads[MAX_THREADS];
thread_t *current_thread;

void mutex_init(mutex_t *m, const char *name) { (void)m; (void)name; }
void mutex_lock(mutex_t *m) { (void)m; }
void mutex_unlock(mutex_t *m) { (void)m; }
void spinlock_init(spinlock_t *lock, const char *name) { (void)lock; (void)name; }
void spinlock_acquire(spinlock_t *lock) { (void)lock; }
void spinlock_release(spinlock_t *lock) { (void)lock; }

time_t get_time(void) { return 0; }
void rusage_init(process_t *p) {
    memset(&p->rusage, 0, sizeof(p->rusage));
    memset(&p->rusage_children, 0, sizeof(p->rusage_children));
}
void proc_timers_init(process_t *p) {
    memset(p->itimer_value_ticks, 0, sizeof(p->itimer_value_ticks));
    memset(p->itimer_interval_ticks, 0, sizeof(p->itimer_interval_ticks));
}
void proc_timers_cancel(process_t *p) {
    memset(p->itimer_value_ticks, 0, sizeof(p->itimer_value_ticks));
    memset(p->itimer_interval_ticks, 0, sizeof(p->itimer_interval_ticks));
}

pmap_t pmap_kernel(void) { return (pmap_t)0xCAFE0000; }
pmap_t pmap_fork(pmap_t src) { return src ? src : (pmap_t)0xBEEF0000; }
void pmap_release(pmap_t pmap) { (void)pmap; }
void pmap_activate(pmap_t pmap) { (void)pmap; }

void *pmm_alloc_block(void) { return NULL; }
void pmm_free_block(void *ptr) { (void)ptr; }
void *pmm_alloc_contiguous(size_t pages) { (void)pages; return NULL; }
int mutex_release_owned_by_thread(thread_t *owner) { (void)owner; return 0; }

void futex_thread_exit(thread_t *t) { (void)t; }

thread_t *sched_create_thread(process_t *proc, void (*entry_point)(void *), void *stack, void *arg) {
    (void)proc;
    (void)entry_point;
    (void)stack;
    (void)arg;
    return NULL;
}

thread_t *sched_alloc_thread(process_t *proc) {
    static int next_tid = 1;
    for (int i = 0; i < MAX_THREADS; i++) {
        if (threads[i].tid != -1) {
            continue;
        }
        memset(&threads[i], 0, sizeof(threads[i]));
        threads[i].tid = next_tid++;
        threads[i].proc = proc;
        threads[i].state = THREAD_BLOCKED;
        threads[i].priority = current_thread ? current_thread->priority : 20;
        threads[i].base_priority = current_thread ? current_thread->base_priority : 20;
        threads[i].sched_class = current_thread ? current_thread->sched_class : SCHED_TIMESHARE;
        threads[i].bound_cpu = -1;
        threads[i].exec_saved_bound_cpu = -1;
        threads[i].sig_mask = current_thread ? current_thread->sig_mask : 0;
        threads[i].sig_pending = 0;
        return &threads[i];
    }
    return NULL;
}

int sched_fork_thread(process_t *proc, void *stack) {
    (void)stack;
    forked_process = proc;
    forked_thread = sched_alloc_thread(proc);
    return forked_thread ? proc->pid : -1;
}

void acct_process(int code) { (void)code; }
void close_fs(fs_node_t *node) { (void)node; }
void rusage_finalize(process_t *p) { (void)p; }
void file_close_ptr(file_t *f) { (void)f; }
int sleepq_wake_all(void *chan) { (void)chan; return 0; }
int sleepq_remove_thread(thread_t *t) { (void)t; return 0; }
void host_wait_for_interrupt(void) {}
void kprint(const char *msg) { (void)msg; }
void tty_hangup(struct tty *tty) { (void)tty; }
void psignal(process_t *p, int sig) { (void)p; (void)sig; }
void vm_map_destroy(struct vm_map *map) { (void)map; }
void pgrp_remove_proc(struct process *proc) { (void)proc; }
void sched_reap_process_threads(process_t *proc) { (void)proc; }
void sched_yield(void) {}
void sched_sleep(void *chan) { (void)chan; }
void sched_wakeup(void *chan) { (void)chan; }

#include "../../sys/pm/process.c"

static void reset_env(void) {
    forked_thread = NULL;
    forked_process = NULL;
    memset(threads, 0, sizeof(threads));
    for (int i = 0; i < MAX_THREADS; i++) {
        threads[i].tid = -1;
        threads[i].state = THREAD_ZOMBIE;
    }
    current_thread = NULL;
    current_process = NULL;
    kernel_process = NULL;
    fs_root = NULL;
    pm_init();
}

static void test_proc_create_clears_reused_signal_state(void) {
    process_t *proc;

    reset_env();

    for (int i = 0; i < 3; i++) {
        processes[i].pid = i + 1;
    }

    memset(&processes[3], 0xA5, sizeof(processes[3]));
    processes[3].pid = -1;

    proc = proc_create(PERS_NATIVE);
    assert(proc == &processes[3]);
    assert(proc->sig_catch == 0);
    assert(proc->sig_ignore == 0);
    assert(proc->p_parent == NULL);
    assert(proc->p_children == NULL);
    assert(proc->p_sibling == NULL);
    assert(proc->vfork_waiter == NULL);
    assert(proc->state == 0);
    assert(proc->p_flag == 0);
    for (int sig = 0; sig < NSIG; sig++) {
        assert(proc->sig_actions[sig].sa_handler == 0);
        assert(proc->sig_actions[sig].sa_mask == 0);
        assert(proc->sig_actions[sig].sa_flags == 0);
    }
}

static void test_fork_inherits_signal_policy_and_mask_only(void) {
    process_t *parent;
    thread_t *parent_thread;
    struct sigaction act;
    int child_pid;

    reset_env();

    parent = proc_create(PERS_NATIVE);
    assert(parent != NULL);
    parent->pmap = (pmap_t)0x12345000;
    strncpy(parent->comm, "parent", AC_COMM_LEN);
    parent->comm[AC_COMM_LEN - 1] = '\0';

    parent_thread = sched_alloc_thread(parent);
    assert(parent_thread != NULL);
    parent_thread->state = THREAD_RUNNING;
    parent_thread->sig_mask = sigmask(SIGINT) | sigmask(SIGTERM);
    parent_thread->sig_pending = sigmask(SIGUSR1) | sigmask(SIGUSR2);

    current_process = parent;
    current_thread = parent_thread;

    memset(&act, 0, sizeof(act));
    act.sa_handler = (sig_t)0x12345678;
    act.sa_mask = sigmask(SIGCHLD);
    act.sa_flags = SA_RESTART | SA_SIGINFO;
    parent->sig_actions[SIGUSR1 - 1] = act;
    parent->sig_actions[SIGWINCH - 1].sa_handler = SIG_IGN;
    parent->sig_ignore = sigmask(SIGWINCH);
    parent->sig_catch = sigmask(SIGUSR1);

    child_pid = proc_fork(parent, NULL);
    assert(child_pid > 0);
    assert(forked_process != NULL);
    assert(forked_thread != NULL);
    assert(forked_process->pid == child_pid);
    assert(forked_process->p_parent == parent);
    assert(parent->p_children == forked_process);
    assert(forked_process->sig_actions[SIGUSR1 - 1].sa_handler == act.sa_handler);
    assert(forked_process->sig_actions[SIGUSR1 - 1].sa_mask == act.sa_mask);
    assert(forked_process->sig_actions[SIGUSR1 - 1].sa_flags == act.sa_flags);
    assert(forked_process->sig_actions[SIGWINCH - 1].sa_handler == SIG_IGN);
    assert(forked_process->sig_catch == parent->sig_catch);
    assert(forked_process->sig_ignore == parent->sig_ignore);
    assert(forked_thread->sig_mask == parent_thread->sig_mask);
    assert(forked_thread->sig_pending == 0);
}

int main(void) {
    test_proc_create_clears_reused_signal_state();
    test_fork_inherits_signal_policy_and_mask_only();
    puts("host_test_proc_signal_state: PASS");
    return 0;
}
