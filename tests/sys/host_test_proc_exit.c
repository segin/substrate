#include <assert.h>
#include <setjmp.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <pm/pm.h>
#include <kern/sched.h>
#include <arch/i386/pmap.h>
#include <sys/tty.h>

process_t processes[MAX_PROCS];
process_t *current_process;
thread_t *current_thread;
process_t *kernel_process;
mutex_t proctree_lock;
thread_t threads[MAX_THREADS];
fs_node_t *fs_root;

static jmp_buf exit_jmp;
static int yielded;
static int acct_calls;
static int futex_exit_calls;
static int close_fs_calls;
static int file_close_calls;
static int rusage_finalize_calls;
static int tty_hangup_calls;
static int vm_map_destroy_calls;
static int pmap_activate_calls;
static int pgrp_remove_calls;
static int wait_for_interrupt_calls;
static process_t *last_psignal_proc;
static int last_psignal_sig;
static void *last_sched_wakeup_chan;
static void *last_sched_sleep_chan;
static void *expected_vfork_wakeup_chan;
static int saw_vfork_wakeup;
static int pmap_release_calls;
static const char *last_kprint_msg;

void mutex_init(mutex_t *m, const char *name) { (void)m; (void)name; }
void mutex_lock(mutex_t *m) { (void)m; }
void mutex_unlock(mutex_t *m) { (void)m; }
void spinlock_init(spinlock_t *lock, const char *name) { (void)lock; (void)name; }
void spinlock_acquire(spinlock_t *lock) { (void)lock; }
void spinlock_release(spinlock_t *lock) { (void)lock; }

uint32_t get_time(void) { return 0; }
void rusage_init(process_t *p) { memset(&p->rusage, 0, sizeof(p->rusage)); }
pmap_t pmap_kernel(void) { return (pmap_t)0xCAFEB000; }
pmap_t pmap_fork(pmap_t src) { return src; }
void pmap_release(pmap_t pmap) { if (pmap) pmap_release_calls++; }
void pmap_activate(pmap_t pmap) { (void)pmap; pmap_activate_calls++; }
void *pmm_alloc_block(void) { return NULL; }
void *pmm_alloc_contiguous(size_t pages) { (void)pages; return NULL; }
int sched_fork_thread(process_t *proc, void *stack) { (void)proc; (void)stack; return -1; }
thread_t *sched_create_thread(process_t *proc, void (*entry_point)(void *), void *stack, void *arg) {
    (void)proc;
    (void)entry_point;
    (void)stack;
    (void)arg;
    return NULL;
}
void futex_thread_exit(thread_t *t) { (void)t; futex_exit_calls++; }
void acct_process(int code) { (void)code; acct_calls++; }
void close_fs(fs_node_t *node) { (void)node; close_fs_calls++; }
void vm_map_destroy(struct vm_map *map) { (void)map; vm_map_destroy_calls++; }
void rusage_finalize(process_t *p) { (void)p; rusage_finalize_calls++; }
void file_close_ptr(file_t *f) { (void)f; file_close_calls++; }
void sched_wakeup(void *chan) {
    last_sched_wakeup_chan = chan;
    if (chan == expected_vfork_wakeup_chan) {
        saw_vfork_wakeup = 1;
    }
}
void sched_sleep(void *chan) { last_sched_sleep_chan = chan; }
void sched_yield(void) { yielded = 1; longjmp(exit_jmp, 1); }
void host_wait_for_interrupt(void) { wait_for_interrupt_calls++; longjmp(exit_jmp, 1); }
void kprint(const char *msg) { last_kprint_msg = msg; }
void tty_hangup(struct tty *tty) { (void)tty; tty_hangup_calls++; }
void psignal(process_t *p, int sig) { last_psignal_proc = p; last_psignal_sig = sig; }
void pgrp_remove_proc(struct process *proc) { if (proc) { proc->p_pgrp = NULL; pgrp_remove_calls++; } }
void sched_reap_process_threads(process_t *proc) {
    for (int i = 0; i < MAX_THREADS; i++) {
        if (threads[i].tid != -1 && threads[i].proc == proc) {
            memset(&threads[i], 0, sizeof(threads[i]));
            threads[i].tid = -1;
            threads[i].state = THREAD_ZOMBIE;
        }
    }
}

#include "../../sys/kern/sleepq.c"
#include "../../sys/pm/process.c"

static void reset_env(void) {
    memset(processes, 0, sizeof(processes));
    memset(threads, 0, sizeof(threads));
    current_process = NULL;
    current_thread = NULL;
    kernel_process = NULL;
    fs_root = (fs_node_t *)0x11110000;
    yielded = 0;
    acct_calls = 0;
    futex_exit_calls = 0;
    close_fs_calls = 0;
    file_close_calls = 0;
    rusage_finalize_calls = 0;
    tty_hangup_calls = 0;
    vm_map_destroy_calls = 0;
    pmap_activate_calls = 0;
    pgrp_remove_calls = 0;
    wait_for_interrupt_calls = 0;
    last_psignal_proc = NULL;
    last_psignal_sig = 0;
    last_sched_wakeup_chan = NULL;
    last_sched_sleep_chan = NULL;
    expected_vfork_wakeup_chan = NULL;
    saw_vfork_wakeup = 0;
    pmap_release_calls = 0;
    last_kprint_msg = NULL;

    for (int i = 0; i < MAX_PROCS; i++) {
        processes[i].pid = -1;
    }
    for (int i = 0; i < MAX_THREADS; i++) {
        threads[i].tid = -1;
    }
    pm_init();
    sleepq_init();
}

static process_t *init_proc(int slot, int pid) {
    process_t *p = &processes[slot];
    memset(p, 0, sizeof(*p));
    p->pid = pid;
    return p;
}

static thread_t *init_thread(int slot, int tid, process_t *proc) {
    thread_t *t = &threads[slot];
    memset(t, 0, sizeof(*t));
    t->tid = tid;
    t->proc = proc;
    t->state = THREAD_RUNNING;
    return t;
}

static void test_proc_exit_basic_path(void) {
    reset_env();

    process_t *swapper = init_proc(0, 0);
    process_t *init = init_proc(1, 1);
    process_t *parent = init_proc(2, 20);
    process_t *proc = init_proc(3, 21);
    process_t *child = init_proc(4, 22);
    struct session sess;
    struct pgrp pgrp;
    struct tty tty;
    int blocked_chan = 0;

    (void)swapper;
    memset(&sess, 0, sizeof(sess));
    memset(&pgrp, 0, sizeof(pgrp));
    memset(&tty, 0, sizeof(tty));

    sess.s_sid = proc->pid;
    sess.s_leader = proc;
    pgrp.pg_id = proc->pid;
    pgrp.pg_session = &sess;
    pgrp.pg_members = proc;
    proc->p_pgrp = &pgrp;
    proc->tty = &tty;
    proc->cwd_node = (fs_node_t *)0x1111;
    proc->root_node = (fs_node_t *)0x2222;
    proc->vm_map = (struct vm_map *)0x3333;
    proc->pmap = (pmap_t)0x4444;
    proc->fds[0] = (file_t *)0x5555;
    proc->fds[1] = (file_t *)0x6666;
    proc->fd_bitmap = 0x3;
    proc->p_parent = parent;
    proc->ppid = parent->pid;
    parent->p_children = proc;
    child->p_parent = proc;
    proc->p_children = child;

    current_process = proc;
    current_thread = init_thread(0, 101, proc);
    thread_t *other = init_thread(1, 102, proc);
    current_thread = other;
    sleepq_add(&blocked_chan, other);
    current_thread = &threads[0];

    if (setjmp(exit_jmp) == 0) {
        proc_exit(42);
        assert(!"proc_exit returned");
    }

    assert(yielded == 1);
    assert(proc->state == SZOMB);
    assert(proc->exit_code == 42);
    assert(acct_calls == 1);
    assert(futex_exit_calls == 2);
    assert(file_close_calls == 2);
    assert(close_fs_calls == 2);
    assert(rusage_finalize_calls == 1);
    assert(pmap_release_calls == 0);
    assert(pmap_activate_calls == 1);
    assert(tty_hangup_calls == 1);
    assert(sess.s_leader == NULL);
    assert(proc->tty == NULL);
    assert(proc->cwd_node == NULL);
    assert(proc->root_node == NULL);
    assert(proc->vm_map == (struct vm_map *)0x3333);
    assert(proc->pmap == pmap_kernel());
    assert(child->p_parent == init);
    assert(init->p_children == child);
    assert(last_psignal_proc == parent);
    assert(last_psignal_sig == SIGCHLD);
    assert(last_sched_wakeup_chan == &parent->p_children);
    assert(!sleepq_has_waiters(&blocked_chan));
    assert(other->wait_chan == NULL);
    assert(current_thread->state == THREAD_ZOMBIE);
    assert(other->state == THREAD_ZOMBIE);
}

static void test_proc_exit_reparents_to_swapper_when_init_dead(void) {
    reset_env();

    process_t *swapper = init_proc(0, 0);
    process_t *init = init_proc(1, 1);
    process_t *parent = init_proc(2, 30);
    process_t *proc = init_proc(3, 31);
    process_t *child = init_proc(4, 32);

    init->state = SDYING;
    proc->p_parent = parent;
    parent->p_children = proc;
    child->p_parent = proc;
    proc->p_children = child;

    current_process = proc;
    current_thread = init_thread(0, 201, proc);

    if (setjmp(exit_jmp) == 0) {
        proc_exit(7);
        assert(!"proc_exit returned");
    }

    assert(yielded == 1);
    assert(child->p_parent == swapper);
    assert(swapper->p_children == child);
}

static void test_proc_exit_autoreap_path(void) {
    reset_env();

    process_t *parent = init_proc(2, 40);
    process_t *proc = init_proc(3, 41);

    parent->sig_actions[SIGCHLD - 1].sa_flags = SA_NOCLDWAIT;
    parent->p_children = proc;
    proc->p_parent = parent;
    proc->ppid = parent->pid;
    proc->vm_map = (struct vm_map *)0x7777;
    proc->pmap = (pmap_t)0x8888;

    current_process = proc;
    current_thread = init_thread(0, 301, proc);

    if (setjmp(exit_jmp) == 0) {
        proc_exit(9);
        assert(!"proc_exit returned");
    }

    assert(proc->state == SZOMB);
    assert(proc->p_flag & P_AUTOREAP);
    assert(parent->p_children == NULL);
    assert(proc->p_parent == NULL);
    assert(vm_map_destroy_calls == 0);

    current_process = parent;
    current_thread = init_thread(1, 302, parent);
    proc_reap_autoreap_zombies();

    assert(vm_map_destroy_calls == 1);
    assert(pgrp_remove_calls == 0);
    assert(proc->pid == -1);
    assert(threads[0].tid == -1);
}

static void test_init_exit_halts_in_idle_loop(void) {
    reset_env();

    process_t *init = init_proc(1, 1);
    current_process = init;
    current_thread = init_thread(0, 401, init);

    if (setjmp(exit_jmp) == 0) {
        proc_exit(1);
        assert(!"proc_exit returned");
    }

    assert(yielded == 0);
    assert(wait_for_interrupt_calls == 1);
    assert(last_kprint_msg != NULL);
}

static void test_vfork_helper_blocks_and_wakes_parent(void) {
    reset_env();

    process_t *parent = init_proc(1, 50);
    process_t *child = init_proc(2, 51);
    thread_t *parent_thread = init_thread(0, 501, parent);

    current_process = parent;
    current_thread = parent_thread;
    child->vfork_waiter = parent_thread;
    expected_vfork_wakeup_chan = child;

    assert(proc_begin_vfork(child) == 0);
    assert(last_sched_sleep_chan == child);

    proc_vfork_done(child);
    assert(child->vfork_waiter == NULL);
    assert(last_sched_wakeup_chan == child);
    assert(saw_vfork_wakeup == 1);
}

static void test_proc_exit_wakes_vfork_parent(void) {
    reset_env();

    process_t *parent = init_proc(1, 60);
    process_t *child = init_proc(2, 61);
    thread_t *parent_thread = init_thread(0, 601, parent);
    thread_t *child_thread = init_thread(1, 602, child);

    child->p_parent = parent;
    parent->p_children = child;
    child->vfork_waiter = parent_thread;
    expected_vfork_wakeup_chan = child;

    current_process = child;
    current_thread = child_thread;

    if (setjmp(exit_jmp) == 0) {
        proc_exit(12);
        assert(!"proc_exit returned");
    }

    assert(child->vfork_waiter == NULL);
    assert(saw_vfork_wakeup == 1);
}

int main(void) {
    test_proc_exit_basic_path();
    test_proc_exit_reparents_to_swapper_when_init_dead();
    test_proc_exit_autoreap_path();
    test_init_exit_halts_in_idle_loop();
    test_vfork_helper_blocks_and_wakes_parent();
    test_proc_exit_wakes_vfork_parent();
    puts("host_test_proc_exit: PASS");
    return 0;
}
