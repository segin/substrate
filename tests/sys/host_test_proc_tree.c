#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <pm/pm.h>
#include <kern/sched.h>
#include <arch/i386/pmap.h>

process_t processes[MAX_PROCS];
process_t *current_process;
thread_t *current_thread;
process_t *kernel_process;
mutex_t proctree_lock;
thread_t threads[MAX_THREADS];
fs_node_t *fs_root;

void mutex_init(mutex_t *m, const char *name) { (void)m; (void)name; }
void mutex_lock(mutex_t *m) { (void)m; }
void mutex_unlock(mutex_t *m) { (void)m; }
void spinlock_init(spinlock_t *lock, const char *name) { (void)lock; (void)name; }
void spinlock_acquire(spinlock_t *lock) { (void)lock; }
void spinlock_release(spinlock_t *lock) { (void)lock; }

time_t get_time(void) { return 0; }
void rusage_init(process_t *p) { memset(&p->rusage, 0, sizeof(p->rusage)); }
void proc_timers_init(process_t *p) {
    memset(p->itimer_value_ticks, 0, sizeof(p->itimer_value_ticks));
    memset(p->itimer_interval_ticks, 0, sizeof(p->itimer_interval_ticks));
}
void proc_timers_cancel(process_t *p) {
    memset(p->itimer_value_ticks, 0, sizeof(p->itimer_value_ticks));
    memset(p->itimer_interval_ticks, 0, sizeof(p->itimer_interval_ticks));
}
pmap_t pmap_kernel(void) { return (pmap_t)1; }
pmap_t pmap_fork(pmap_t src) { return src; }
void pmap_release(pmap_t pmap) { (void)pmap; }
void pmap_activate(pmap_t pmap) { (void)pmap; }
void *pmm_alloc_block(void) { return NULL; }
void pmm_free_block(void *ptr) { (void)ptr; }
void *pmm_alloc_contiguous(size_t pages) { (void)pages; return NULL; }
int mutex_release_owned_by_thread(thread_t *owner) { (void)owner; return 0; }
int sched_fork_thread(process_t *proc, void *stack) { (void)proc; (void)stack; return -1; }
thread_t *sched_create_thread(process_t *proc, void (*entry_point)(void *), void *stack, void *arg) {
    (void)proc;
    (void)entry_point;
    (void)stack;
    (void)arg;
    return NULL;
}
void futex_thread_exit(thread_t *t) { (void)t; }
void acct_process(int code) { (void)code; }
void close_fs(fs_node_t *node) { (void)node; }
void rusage_finalize(process_t *p) { (void)p; }
void file_close_ptr(file_t *f) { (void)f; }
int sleepq_wake_all(void *chan) { (void)chan; return 0; }
int sleepq_remove_thread(thread_t *t) { (void)t; return 0; }
void sched_sleep(void *chan) { (void)chan; }
void sched_wakeup(void *chan) { (void)chan; }
void sched_yield(void) {}
void host_wait_for_interrupt(void) {}
void kprint(const char *msg) { (void)msg; }
void tty_hangup(struct tty *tty) { (void)tty; }
void vm_map_destroy(struct vm_map *map) { (void)map; }
void pgrp_remove_proc(struct process *proc) { (void)proc; }
void sched_reap_process_threads(process_t *proc) { (void)proc; }

#include "../../sys/pm/process.c"

static void reset_env(void) {
    memset(processes, 0, sizeof(processes));
    for (int i = 0; i < MAX_PROCS; i++) {
        processes[i].pid = -1;
    }
    memset(threads, 0, sizeof(threads));
    for (int i = 0; i < MAX_THREADS; i++) {
        threads[i].tid = -1;
    }
    pm_init();
}

static process_t *init_proc(int slot, int pid) {
    process_t *p = &processes[slot];
    memset(p, 0, sizeof(*p));
    p->pid = pid;
    return p;
}

static void test_reparent_to_init(void) {
    reset_env();

    process_t *swapper = init_proc(0, 0);
    process_t *init = init_proc(1, 1);
    process_t *parent = init_proc(2, 10);
    process_t *child1 = init_proc(3, 11);
    process_t *child2 = init_proc(4, 12);

    parent->p_children = child1;
    child1->p_parent = parent;
    child1->p_sibling = child2;
    child2->p_parent = parent;
    child2->p_sibling = NULL;
    child2->state = SZOMB;

    (void)swapper;
    proc_reparent_children(parent);

    assert(parent->p_children == NULL);
    assert(child1->p_parent == init);
    assert(child2->p_parent == init);
    assert(init->p_children == child2);
    assert(child2->p_sibling == child1);
}

static void test_reparent_to_swapper_when_init_dying(void) {
    reset_env();

    process_t *swapper = init_proc(0, 0);
    process_t *init = init_proc(1, 1);
    process_t *parent = init_proc(2, 20);
    process_t *child = init_proc(3, 21);

    init->state = SDYING;
    parent->p_children = child;
    child->p_parent = parent;

    proc_reparent_children(parent);

    assert(parent->p_children == NULL);
    assert(child->p_parent == swapper);
    assert(swapper->p_children == child);
}

int main(void) {
    test_reparent_to_init();
    test_reparent_to_swapper_when_init_dying();
    puts("host_test_proc_tree: PASS");
    return 0;
}
