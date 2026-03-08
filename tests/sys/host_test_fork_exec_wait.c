#include <assert.h>
#include <setjmp.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <pm/pm.h>
#include <kern/sched.h>
#include <arch/i386/pmap.h>
#include <sys/exec.h>
#include <sys/file.h>
#include <sys/fcntl.h>
#include <sys/signal.h>
#include <sys/session.h>
#include <sys/wait.h>

thread_t threads[MAX_THREADS];
thread_t *current_thread;
fs_node_t *fs_root;

static jmp_buf exit_jmp;
static thread_t *forked_thread;
static process_t *forked_process;
static file_t fake_file;
static int exec_load_calls;
static int exec_open_calls;
static int exec_close_calls;
static int vm_map_destroy_calls;
static int yielded;

void mutex_init(mutex_t *m, const char *name) { (void)m; (void)name; }
void mutex_lock(mutex_t *m) { (void)m; }
void mutex_unlock(mutex_t *m) { (void)m; }
void spinlock_init(spinlock_t *lock, const char *name) { (void)lock; (void)name; }
void spinlock_acquire(spinlock_t *lock) { (void)lock; }
void spinlock_release(spinlock_t *lock) { (void)lock; }

uint32_t get_time(void) { return 0; }
void rusage_init(process_t *p) {
    memset(&p->rusage, 0, sizeof(p->rusage));
    memset(&p->rusage_children, 0, sizeof(p->rusage_children));
}
void rusage_finalize(process_t *p) { (void)p; }
void acct_process(int code) { (void)code; }
void close_fs(fs_node_t *node) { (void)node; }
void file_close_ptr(file_t *f) { (void)f; }
void tty_hangup(struct tty *tty) { (void)tty; }
void futex_thread_exit(thread_t *t) { (void)t; }
void kprint(const char *msg) { (void)msg; }
void host_wait_for_interrupt(void) {}
int smp_get_cpu_id(void) { return 1; }
int copyout(const void *src, void *dst, size_t len) { memcpy(dst, src, len); return 0; }
void psignal(process_t *p, int sig) { (void)p; (void)sig; }

pmap_t pmap_kernel(void) { return (pmap_t)0xCAFE0000; }
pmap_t pmap_fork(pmap_t src) { return src ? src : (pmap_t)0xBEEF0000; }
void pmap_release(pmap_t pmap) { (void)pmap; }
void pmap_activate(pmap_t pmap) { (void)pmap; }

void *pmm_alloc_block(void) { return NULL; }
void *pmm_alloc_contiguous(size_t pages) { (void)pages; return NULL; }

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

void sched_wakeup(void *chan) { (void)chan; }
void sched_sleep(void *chan) { (void)chan; }
void sched_yield(void) { yielded = 1; longjmp(exit_jmp, 1); }

int vfs_check_permissions(fs_node_t *node, uint32_t uid, uint32_t gid, int mode) {
    (void)node;
    (void)uid;
    (void)gid;
    (void)mode;
    return 0;
}

int kern_open(const char *path, int flags, int mode) {
    (void)path;
    (void)flags;
    (void)mode;

    exec_open_calls++;
    memset(&fake_file, 0, sizeof(fake_file));
    fake_file.f_count = 1;
    fake_file.f_data = (void *)(uintptr_t)0x12340000;
    current_process->fds[3] = &fake_file;
    current_process->fd_bitmap |= (1U << 3);
    return 3;
}

int kern_read(int fd, char *buf, int len) {
    static const unsigned char header[] = {0x7f, 'E', 'L', 'F', 1, 1, 1, 0};

    (void)fd;
    if (len < (int)sizeof(header)) {
        return -1;
    }
    memcpy(buf, header, sizeof(header));
    return (int)sizeof(header);
}

int kern_close(int fd) {
    exec_close_calls++;
    if (fd >= 0 && fd < MAX_FD) {
        current_process->fds[fd] = NULL;
        current_process->fd_bitmap &= ~(1U << fd);
    }
    return 0;
}

void vm_map_destroy(struct vm_map *map) {
    (void)map;
    vm_map_destroy_calls++;
}

void pgrp_remove_proc(struct process *proc) {
    if (!proc || !proc->p_pgrp) {
        return;
    }

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

static int fake_exec_check(const char *path, const char *header_buf, size_t len) {
    (void)path;
    if (len < 4) {
        return -1;
    }
    return (header_buf[0] == 0x7f &&
            header_buf[1] == 'E' &&
            header_buf[2] == 'L' &&
            header_buf[3] == 'F') ? 0 : -1;
}

static int fake_exec_load(int fd, const char *path, char *const argv[], char *const envp[]) {
    (void)fd;
    (void)path;
    (void)envp;

    exec_load_calls++;
    assert(current_thread != NULL);
    assert(current_thread->exec_pin_active == 1);
    assert(current_thread->bound_cpu == 1);
    assert((current_thread->flags & THREAD_F_NO_PREEMPT) != 0);
    assert(argv != NULL);
    assert(argv[0] != NULL);

    strncpy(current_process->comm, "exec-child", AC_COMM_LEN);
    current_process->comm[AC_COMM_LEN - 1] = '\0';
    return 0;
}

int elf_execve(int fd, const char *path, char *const argv[], char *const envp[]) {
    (void)fd;
    (void)path;
    (void)argv;
    (void)envp;
    return -1;
}

int elks_check_file(const char *path, const char *header_buf, size_t len) {
    (void)path;
    (void)header_buf;
    (void)len;
    return -1;
}

int elks_load(int fd, const char *path, char *const argv[], char *const envp[]) {
    (void)fd;
    (void)path;
    (void)argv;
    (void)envp;
    return -1;
}

#include "../../sys/kern/sleepq.c"
#include "../../sys/pm/process.c"
#include "../../sys/pm/wait.c"
#include "../../sys/exec/exec.c"

static int kern_execve_host(const char *path, char *const argv[], char *const envp[]) {
    exec_pin_current_thread();
    int ret = exec_dispatch(path, argv, envp);
    if (ret == 0) {
        proc_vfork_done(current_process);
    }
    exec_unpin_current_thread();
    return ret;
}

static void reset_env(void) {
    static struct exec_binary_handler fake_handler = {
        .name = "fake-elf",
        .check = fake_exec_check,
        .load = fake_exec_load,
        .next = NULL,
    };

    forked_thread = NULL;
    forked_process = NULL;
    exec_load_calls = 0;
    exec_open_calls = 0;
    exec_close_calls = 0;
    vm_map_destroy_calls = 0;
    yielded = 0;

    memset(threads, 0, sizeof(threads));
    for (int i = 0; i < MAX_THREADS; i++) {
        threads[i].tid = -1;
        threads[i].state = THREAD_ZOMBIE;
    }

    current_thread = NULL;
    current_process = NULL;
    kernel_process = NULL;
    fs_root = NULL;
    exec_handlers = NULL;
    pm_init();
    sleepq_init();
    exec_register_handler(&fake_handler);
}

static void test_fork_exec_exit_wait_cycle(void) {
    int status;
    char *argv[] = {(char *)"/bin/fake", NULL};
    char *envp[] = {NULL};

    reset_env();

    process_t *parent = proc_create(PERS_NATIVE);
    assert(parent != NULL);
    parent->pmap = (pmap_t)0x12345000;
    strncpy(parent->comm, "parent", AC_COMM_LEN);
    parent->comm[AC_COMM_LEN - 1] = '\0';

    thread_t *parent_thread = sched_alloc_thread(parent);
    assert(parent_thread != NULL);
    parent_thread->state = THREAD_RUNNING;
    parent_thread->priority = 12;
    parent_thread->base_priority = 12;
    parent_thread->sched_class = SCHED_TIMESHARE;
    parent_thread->bound_cpu = -1;
    parent_thread->exec_saved_bound_cpu = -1;

    current_process = parent;
    current_thread = parent_thread;

    int child_pid = proc_fork(parent, NULL);
    assert(child_pid > 0);
    assert(forked_process != NULL);
    assert(forked_thread != NULL);
    assert(parent->p_children == forked_process);

    forked_process->vm_map = (struct vm_map *)0x87650000;
    forked_process->pmap = (pmap_t)0x12345000;

    current_process = forked_process;
    current_thread = forked_thread;

    assert(kern_execve_host("/bin/fake", argv, envp) == 0);
    assert(exec_open_calls == 1);
    assert(exec_load_calls == 1);
    assert(exec_close_calls == 0);
    assert(strcmp(current_process->comm, "exec-child") == 0);
    assert(current_thread->exec_pin_active == 0);
    assert(current_thread->bound_cpu == -1);
    assert((current_thread->flags & THREAD_F_NO_PREEMPT) == 0);

    if (setjmp(exit_jmp) == 0) {
        proc_exit(33);
        assert(!"proc_exit returned");
    }

    assert(yielded == 1);
    assert(forked_process->state == SZOMB);

    current_process = parent;
    current_thread = parent_thread;

    int waited_pid = kern_wait4(child_pid, &status, 0, NULL);
    assert(waited_pid == child_pid);
    assert(WEXITSTATUS(status) == 33);
    assert(parent->p_children == NULL);
    assert(forked_process->pid == -1);
    assert(forked_thread->tid == -1);
    assert(vm_map_destroy_calls == 1);
}

int main(void) {
    test_fork_exec_exit_wait_cycle();
    puts("host_test_fork_exec_wait: PASS");
    return 0;
}
