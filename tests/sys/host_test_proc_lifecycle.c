#include <assert.h>
#include <setjmp.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <pm/pm.h>
#include <kern/sched.h>
#include <sys/wait.h>
#include <sys/session.h>
#include <arch/i386/pmap.h>
#include <vm/vm_map.h>

process_t processes[MAX_PROCS];
process_t *current_process;
thread_t *current_thread;
process_t *kernel_process;
thread_t threads[MAX_THREADS];
mutex_t proctree_lock;
fs_node_t *fs_root;

static jmp_buf exit_jmp;
static int yielded;
static int vm_map_destroy_calls;

void mutex_init(mutex_t *m, const char *name) { (void)m; (void)name; }
void mutex_lock(mutex_t *m) { (void)m; }
void mutex_unlock(mutex_t *m) { (void)m; }
void spinlock_init(spinlock_t *lock, const char *name) { (void)lock; (void)name; }
void spinlock_acquire(spinlock_t *lock) { (void)lock; }
void spinlock_release(spinlock_t *lock) { (void)lock; }

uint32_t get_time(void) { return 0; }
void rusage_init(process_t *p) { memset(&p->rusage, 0, sizeof(p->rusage)); memset(&p->rusage_children, 0, sizeof(p->rusage_children)); }
void rusage_finalize(process_t *p) { (void)p; }
void acct_process(int code) { (void)code; }
void futex_thread_exit(thread_t *t) { (void)t; }
void close_fs(fs_node_t *node) { (void)node; }
void file_close_ptr(file_t *f) { (void)f; }
void tty_hangup(struct tty *tty) { (void)tty; }
void kprint(const char *msg) { (void)msg; }
void host_wait_for_interrupt(void) { longjmp(exit_jmp, 2); }

pmap_t pmap_kernel(void) { return (pmap_t)0xCAFE0000; }
pmap_t pmap_fork(pmap_t src) { return src; }
void pmap_release(pmap_t pmap) { (void)pmap; }
void pmap_activate(pmap_t pmap) { (void)pmap; }
void *pmm_alloc_block(void) { return NULL; }
void *pmm_alloc_contiguous(size_t pages) { (void)pages; return NULL; }
int sched_fork_thread(process_t *proc, void *stack) { (void)proc; (void)stack; return -1; }
thread_t *sched_create_thread(process_t *proc, void (*entry_point)(void *), void *stack, void *arg) {
    (void)proc; (void)entry_point; (void)stack; (void)arg; return NULL;
}

int copyout(const void *src, void *dst, size_t len) { memcpy(dst, src, len); return 0; }
void vm_map_destroy(vm_map_t *map) { (void)map; vm_map_destroy_calls++; }
void sched_wakeup(void *chan) { (void)chan; }
void sched_sleep(void *chan) { (void)chan; }
void sched_yield(void) { yielded = 1; longjmp(exit_jmp, 1); }
void psignal(process_t *proc, int sig) {
    if (proc && sig > 0 && sig < NSIG) {
        if (current_thread) current_thread->sig_pending |= sigmask(sig);
    }
}

void pgrp_remove_proc(struct process *proc) {
    if (!proc || !proc->p_pgrp) return;

    struct pgrp *pgrp = proc->p_pgrp;
    struct process **pp = &pgrp->pg_members;
    while (*pp && *pp != proc) {
        pp = &(*pp)->p_pgrp_link;
    }
    if (*pp) {
        *pp = proc->p_pgrp_link;
    }
    proc->p_pgrp = NULL;
    proc->p_pgrp_link = NULL;
}

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
#include "../../sys/pm/wait.c"

static void reset_env(void) {
    memset(processes, 0, sizeof(processes));
    memset(threads, 0, sizeof(threads));
    yielded = 0;
    vm_map_destroy_calls = 0;
    current_process = NULL;
    current_thread = NULL;
    kernel_process = NULL;
    fs_root = NULL;

    for (int i = 0; i < MAX_PROCS; i++) {
        processes[i].pid = -1;
    }
    for (int i = 0; i < MAX_THREADS; i++) {
        threads[i].tid = -1;
    }

    pm_init();
    sleepq_init();
}

static process_t *init_proc(int slot, int pid, int ppid) {
    process_t *p = &processes[slot];
    memset(p, 0, sizeof(*p));
    p->pid = pid;
    p->ppid = ppid;
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

static void test_no_zombie_leaks_after_reap_cycles(void) {
    int status;

    reset_env();

    process_t *parent = init_proc(1, 10, 0);
    current_process = parent;
    current_thread = init_thread(0, 100, parent);

    for (int i = 0; i < 8; i++) {
        process_t *child = init_proc(2, 100 + i, parent->pid);
        int expected_pid = child->pid;
        child->state = SZOMB;
        child->exit_code = i;
        child->p_parent = parent;
        child->vm_map = (vm_map_t *)(uintptr_t)(0x1000 + i);
        child->pmap = (pmap_t)(uintptr_t)(0x2000 + i);
        parent->p_children = child;

        assert(kern_wait4(-1, &status, 0, NULL) == expected_pid);
        assert(WEXITSTATUS(status) == i);
        assert(parent->p_children == NULL);
        assert(child->pid == -1);
    }
}

static void test_no_zombie_leaks_after_autoreap_cycles(void) {
    reset_env();

    process_t *parent = init_proc(1, 20, 0);
    parent->sig_actions[SIGCHLD - 1].sa_flags = SA_NOCLDWAIT;
    current_process = parent;
    current_thread = init_thread(0, 200, parent);

    for (int i = 0; i < 8; i++) {
        process_t *child = init_proc(2, 200 + i, parent->pid);
        thread_t *child_thread = init_thread(1, 300 + i, child);
        child->p_parent = parent;
        child->vm_map = (vm_map_t *)(uintptr_t)(0x3000 + i);
        child->pmap = (pmap_t)(uintptr_t)(0x4000 + i);
        parent->p_children = child;
        current_process = child;
        current_thread = child_thread;

        int rc = setjmp(exit_jmp);
        if (rc == 0) {
            proc_exit(70 + i);
            assert(!"proc_exit returned");
        }
        assert(rc == 1);

        current_process = parent;
        current_thread = &threads[0];
        proc_reap_autoreap_zombies();

        assert(parent->p_children == NULL);
        assert(child->pid == -1);
        assert(child_thread->tid == -1);
    }
}

static void test_waitpid_job_control_lifecycle(void) {
    int status;

    reset_env();

    process_t *parent = init_proc(1, 30, 0);
    process_t *child = init_proc(2, 31, parent->pid);
    thread_t *parent_thread = init_thread(0, 400, parent);

    child->p_parent = parent;
    parent->p_children = child;
    current_process = parent;
    current_thread = parent_thread;

    child->state = SSTOP;
    child->p_flag = 0;
    assert(kern_wait4(child->pid, &status, WUNTRACED, NULL) == child->pid);
    assert(WIFSTOPPED(status));
    assert(child->p_flag & P_WAITED);

    child->state = SRUN;
    child->p_flag |= P_CONTINUED;
    assert(kern_wait4(child->pid, &status, WCONTINUED, NULL) == child->pid);
    assert(WIFCONTINUED(status));
    assert((child->p_flag & P_CONTINUED) == 0);

    child->state = SZOMB;
    child->exit_code = 55;
    child->vm_map = (vm_map_t *)0x7777;
    child->pmap = (pmap_t)0x8888;
    int expected_pid = child->pid;
    assert(kern_wait4(child->pid, &status, 0, NULL) == expected_pid);
    assert(WEXITSTATUS(status) == 55);
    assert(parent->p_children == NULL);
    assert(child->pid == -1);
}

int main(void) {
    test_no_zombie_leaks_after_reap_cycles();
    test_no_zombie_leaks_after_autoreap_cycles();
    test_waitpid_job_control_lifecycle();
    puts("host_test_proc_lifecycle: PASS");
    return 0;
}
