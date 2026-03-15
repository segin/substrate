#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <pm/pm.h>
#include <kern/sched.h>
#include <arch/i386/pmap.h>
#include <sys/fcntl.h>
#include <sys/file.h>

process_t processes[MAX_PROCS];
process_t *current_process;
thread_t *current_thread;
process_t *kernel_process;
mutex_t proctree_lock;
thread_t threads[MAX_THREADS];
fs_node_t *fs_root;

static int file_close_calls;

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
void file_close_ptr(file_t *f) { (void)f; file_close_calls++; }
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
void ldt_init_process(process_t *proc) { memset(&proc->ldt_lock, 0, sizeof(proc->ldt_lock)); }
int ldt_clone_process(process_t *child, const process_t *parent) { (void)child; (void)parent; return 0; }
void ldt_free_process(process_t *proc) { (void)proc; }
void open_fs(fs_node_t *node, uint8_t read, uint8_t write) {
    (void)node;
    (void)read;
    (void)write;
}
struct vm_map *vm_map_fork(struct vm_map *src, pmap_t pmap) {
    (void)src;
    (void)pmap;
    return NULL;
}

#include "../../sys/pm/process.c"

static void reset_env(void) {
    memset(processes, 0, sizeof(processes));
    memset(threads, 0, sizeof(threads));
    current_process = NULL;
    current_thread = NULL;
    kernel_process = NULL;
    fs_root = NULL;
    file_close_calls = 0;

    for (int i = 0; i < MAX_PROCS; i++) {
        processes[i].pid = -1;
    }
    for (int i = 0; i < MAX_THREADS; i++) {
        threads[i].tid = -1;
    }
    pm_init();
}

static process_t *init_proc(int slot, int pid) {
    process_t *p = &processes[slot];
    memset(p, 0, sizeof(*p));
    p->pid = pid;
    p->next_fd = 0;
    return p;
}

static void test_proc_fcntl_status_and_dup(void) {
    process_t *proc;
    file_t file;

    reset_env();
    proc = init_proc(2, 42);
    memset(&file, 0, sizeof(file));
    file.f_count = 1;
    file.f_flag = FREAD | FWRITE | FAPPEND;

    proc->fds[3] = &file;
    proc->fd_bitmap = (1U << 3);
    proc->next_fd = 0;

    assert(proc_fcntl(proc, 3, F_GETFL, 0) == (O_RDWR | O_APPEND));

    assert(proc_fcntl(proc, 3, F_SETFL, O_NONBLOCK) == 0);
    assert(proc_fcntl(proc, 3, F_GETFL, 0) == (O_RDWR | O_NONBLOCK));
    assert((file.f_flag & (FREAD | FWRITE)) == (FREAD | FWRITE));
    assert((file.f_flag & FAPPEND) == 0);
    assert((file.f_flag & FNONBLOCK) != 0);

    assert(proc_fcntl(proc, 3, F_GETFD, 0) == 0);
    assert(proc_fcntl(proc, 3, F_SETFD, FD_CLOEXEC) == 0);
    assert(proc_fcntl(proc, 3, F_GETFD, 0) == FD_CLOEXEC);
    assert((proc->fd_cloexec & (1U << 3)) != 0);

    assert(proc_fcntl(proc, 3, F_DUPFD, 5) == 5);
    assert(proc->fds[5] == &file);
    assert(file.f_count == 2);
    assert(proc_fcntl(proc, 5, F_GETFD, 0) == 0);
    assert(proc_fcntl(proc, 3, F_DUPFD, MAX_FD) == -EINVAL);
}

static void test_proc_close_cloexec(void) {
    process_t *proc;
    file_t clo_file;
    file_t keep_file;

    reset_env();
    proc = init_proc(2, 43);
    memset(&clo_file, 0, sizeof(clo_file));
    memset(&keep_file, 0, sizeof(keep_file));

    clo_file.f_flag = FREAD;
    clo_file.f_count = 1;
    keep_file.f_flag = FREAD | FWRITE;
    keep_file.f_count = 1;

    proc->fds[1] = &clo_file;
    proc->fds[4] = &keep_file;
    proc->fd_bitmap = (1U << 1) | (1U << 4);
    proc->fd_cloexec = (1U << 1);
    proc->next_fd = 7;

    proc_close_cloexec(proc);

    assert(file_close_calls == 1);
    assert(proc->fds[1] == NULL);
    assert(proc->fds[4] == &keep_file);
    assert((proc->fd_bitmap & (1U << 1)) == 0);
    assert((proc->fd_bitmap & (1U << 4)) != 0);
    assert((proc->fd_cloexec & (1U << 1)) == 0);
    assert(proc->next_fd == 1);
}

int main(void) {
    test_proc_fcntl_status_and_dup();
    test_proc_close_cloexec();
    puts("host_test_proc_fcntl: PASS");
    return 0;
}
