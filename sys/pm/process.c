#include <stddef.h>
#include <string.h>
#ifndef HOST_TEST
#include <kern/cmdline.h>
#include <vm/vm_kmem.h>
#else
#include <stdlib.h>
#endif

#include <sys/acct.h>
#include <sys/copy.h>
#include <sys/fcntl.h>
#include <sys/file.h>
#include <sys/futex.h>
#include <sys/ldt.h>
#include <sys/lock.h>
#include <sys/mqueue.h>
#include <sys/posix_sem.h>
#include <sys/preempt.h>
#include <sys/random.h>
#include <sys/sem.h>
#include <sys/session.h>
#include <sys/shm.h>
#include <sys/signal.h>
#include <sys/tty.h>
#include <sys/types.h>
#include <sys/vt.h>
#include <vm/vm_commit.h>
#include <vm/vm_map.h>
#include <vm/vm_pager.h>
#include <vfs/buf.h>
#include <vfs/vfs.h>
#include <fs/ext2/ext2.h>
#include <pm/pm.h>
#include <kern/console.h>
#include <kern/file.h>
#include <kern/main.h>
#include <kern/sched.h>
#include <kern/sleepq.h>
#include <kern/time.h>
#include <arch/i386/intr.h>
#include <arch/i386/pmap.h>
#include <arch/i386/pmm.h>
#include <arch/i386/fpu/fpu_emu.h>
#include <exec/perso/personality.h>

/* current_process is per-CPU: a macro over curproc_slot() (arch percpu.c). */
process_t *kernel_process = NULL;

/*
 * Process registry — every live process_t is on `allproc` (head-linked
 * singly-linked list) and in exactly one `pid_hash[]` bucket.  Both
 * are protected by pid_lock.
 */
#define PID_HASH_SIZE 64
#define PID_HASH(pid) ((unsigned)(pid) & (PID_HASH_SIZE - 1))

static process_t *allproc = NULL;
static process_t *pid_hash[PID_HASH_SIZE];
static int next_pid = 1;
static int last_pid = 0;
static spinlock_t pid_lock;

/* Count of SA_NOCLDWAIT (P_AUTOREAP) exiting processes still awaiting the
 * idle-loop autoreap sweep.  proc_reap_autoreap_zombies() runs from every
 * sched_yield(); this lets it skip its (now pid_lock-protected) allproc scan
 * outright in the overwhelmingly common case that there is nothing to reap,
 * so the sweep adds no per-context-switch pid_lock traffic during a fork
 * storm.  Bumped where P_AUTOREAP is set, dropped as each victim is reaped. */
static volatile uint32_t autoreap_pending = 0;

#ifdef HOST_TEST
static process_t *proc_storage_alloc(void) {
    process_t *p = malloc(sizeof(*p));
    if (p) memset(p, 0, sizeof(*p));
    return p;
}
static void proc_storage_free(process_t *p) { free(p); }
#else
static process_t *proc_storage_alloc(void) {
    process_t *p = kmalloc(sizeof(*p));
    if (p) {
        memset(p, 0, sizeof(*p));
        p->state = SSLEEP;   /* default S until scheduled/blocked (for ps) */
    }
    return p;
}
static void proc_storage_free(process_t *p) {
    if (p) kfree(p, sizeof(*p));
}
#endif


/*
 * proctree_lock - Protects the process tree structure
 *
 * This mutex must be held when modifying the process hierarchy:
 * - p_parent, p_children, p_sibling pointers
 * - Reparenting operations (orphan handling)
 * - Process group membership changes
 *
 * Use mutex (not spinlock) since operations may sleep or take time.
 */
mutex_t proctree_lock;



#ifdef HOST_TEST
#define PROC_DEBUG_ENABLED() 0
#define PROC_DEBUG(...) ((void)0)
#else
#define PROC_DEBUG_ENABLED() \
    (cmdline_debug_enabled("proc") || cmdline_debug_enabled("perso:linux"))
#define PROC_DEBUG(...) kprintf(__VA_ARGS__)
#endif

static void proc_idle_wait(void) {
#ifdef HOST_TEST
    host_wait_for_interrupt();
#else
    wait_for_interrupt();
#endif
}

/* Forward declarations */
void proc_add_child(process_t *parent, process_t *child);
void proc_remove_child(process_t *parent, process_t *child);
static int proc_threads_all_zombie(process_t *proc, thread_t *skip_thread);
static void proc_release_zombie_resources(process_t *proc);
static int proc_fork_common(process_t *parent, void *stack, int is_vfork);
static void proc_sysvipc_exit(process_t *proc);
static void proc_posixipc_exit(process_t *proc);
static int proc_status_flags_from_file(const file_t *f);
static void proc_apply_status_flags(file_t *f, int flags);
static void proc_resource_limits_init(process_t *proc);

/*
 * Link `proc` onto allproc and pid_hash[].  pid_lock must be held.
 */
static void proc_link_locked(process_t *proc) {
    proc->p_allproc_next = allproc;
    allproc = proc;

    unsigned bucket = PID_HASH(proc->pid);
    proc->p_pidhash_next = pid_hash[bucket];
    pid_hash[bucket] = proc;
}

/*
 * Unlink `proc` from allproc and pid_hash[].  pid_lock must be held.
 */
static void proc_unlink_locked(process_t *proc) {
    process_t **link;

    for (link = &allproc; *link; link = &(*link)->p_allproc_next) {
        if (*link == proc) {
            *link = proc->p_allproc_next;
            break;
        }
    }
    proc->p_allproc_next = NULL;

    unsigned bucket = PID_HASH(proc->pid);
    for (link = &pid_hash[bucket]; *link; link = &(*link)->p_pidhash_next) {
        if (*link == proc) {
            *link = proc->p_pidhash_next;
            break;
        }
    }
    proc->p_pidhash_next = NULL;
}

static process_t *proc_lookup_locked(int pid) {
    for (process_t *p = pid_hash[PID_HASH(pid)]; p; p = p->p_pidhash_next) {
        if (p->pid == pid) {
            return p;
        }
    }
    return NULL;
}

process_t *proc_first(void) {
    return allproc;
}

process_t *proc_next(process_t *p) {
    return p ? p->p_allproc_next : NULL;
}

static const char *proc_cmdline_name(const process_t *proc) {
    const char *name;
    const char *cursor;

    if (!proc) {
        return "unknown";
    }
    if (proc->comm[0] != '\0') {
        return proc->comm;
    }
    name = proc->exec_path;
    for (cursor = proc->exec_path; *cursor != '\0'; cursor++) {
        if (*cursor == '/') {
            name = cursor + 1;
        }
    }
    if (name[0] != '\0') {
        return name;
    }
    return "unknown";
}

static int proc_alloc_pid_locked(void) {
    int start;
    int candidate;

    start = next_pid;
    if (start < 1 || start > SUBSTRATE_PID_MAX) {
        start = 1;
    }

    candidate = start;
    do {
        if (!proc_lookup_locked(candidate)) {
            last_pid = candidate;
            next_pid = (candidate == SUBSTRATE_PID_MAX) ? 1 : candidate + 1;
            return candidate;
        }

        candidate++;
        if (candidate > SUBSTRATE_PID_MAX) {
            candidate = 1;
        }
    } while (candidate != start);

    return -1;
}

void proc_capture_cmdline(process_t *proc, char *const argv[]) {
    size_t used;
    int i;

    if (!proc) {
        return;
    }

    proc->cmdline_tail_len = 0;
    proc->cmdline_tail_argc = 0;
    memset(proc->cmdline_tail, 0, sizeof(proc->cmdline_tail));

    if (!argv) {
        return;
    }

    /* Capture the WHOLE argv (argv[0], argv[1], ...) as NUL-separated
     * strings, not just the tail -- comm[] truncates argv[0] to 16 bytes,
     * so a long program name (or a login shell's "-ksh", a busybox applet,
     * etc.) was otherwise lost.  ps and /proc/<pid>/cmdline reconstruct the
     * full command line from this blob. */
    used = 0;
    for (i = 0; argv[i] != NULL && used < sizeof(proc->cmdline_tail); i++) {
        size_t avail = sizeof(proc->cmdline_tail) - used;
        size_t copy_len;

        if (avail <= 1U) {
            break;
        }

        copy_len = strnlen(argv[i], avail - 1U);
        memcpy(proc->cmdline_tail + used, argv[i], copy_len);
        proc->cmdline_tail[used + copy_len] = '\0';
        used += copy_len + 1U;
        proc->cmdline_tail_len = (uint16_t)used;
        proc->cmdline_tail_argc++;

        if (argv[i][copy_len] != '\0') {
            break;
        }
    }
}

size_t proc_emit_cmdline(const process_t *proc, char *buf, size_t buf_len, size_t *argc_out) {
    size_t limit;

    if (argc_out) {
        *argc_out = 0;
    }
    if (!proc || !buf || buf_len == 0) {
        return 0;
    }

    limit = buf_len;
    if (limit > PROC_CMDLINE_MAX) {
        limit = PROC_CMDLINE_MAX;
    }

    /* cmdline_tail now holds the whole argv (argv[0], argv[1], ...) as
     * NUL-separated strings; emit it verbatim. */
    if (proc->cmdline_tail_len > 0) {
        size_t n = proc->cmdline_tail_len;
        if (n > limit) {
            n = limit;
        }
        memcpy(buf, proc->cmdline_tail, n);
        buf[(n < buf_len) ? n : (buf_len - 1U)] = '\0';
        if (argc_out) {
            *argc_out = proc->cmdline_tail_argc;
        }
        return n;
    }

    /* No captured argv (e.g. a kernel thread): fall back to the comm name. */
    {
        const char *name = proc_cmdline_name(proc);
        size_t name_len = strnlen(name, limit - 1U);
        memcpy(buf, name, name_len);
        buf[name_len] = '\0';
        if (argc_out) {
            *argc_out = (name_len > 0) ? 1U : 0U;
        }
        return name_len + 1U;
    }
}

static void proc_resource_limits_init(process_t *proc) {
    if (!proc) {
        return;
    }

    proc->rlimits[RLIMIT_CORE].rlim_cur = RLIM_INFINITY;
    proc->rlimits[RLIMIT_CORE].rlim_max = RLIM_INFINITY;

    /* No memlock limit and no mlockall() by default. */
    proc->rlim_memlock_cur = RLIM_INFINITY;
    proc->rlim_memlock_max = RLIM_INFINITY;
    proc->rlim_as_cur      = RLIM_INFINITY;
    proc->rlim_as_max      = RLIM_INFINITY;
    proc->mlockall_flags   = 0;
}

void pm_init(void) {
    next_pid = 1;
    last_pid = 0;
    allproc = NULL;
    memset(pid_hash, 0, sizeof(pid_hash));

    mutex_init(&proctree_lock, "proctree");
    spinlock_init(&pid_lock, "pid");
}

/*
 * Bootstrap path for the swapper/kernel process.  Called exactly
 * once from sched_init() after kmalloc is up.  The caller fills in
 * pmap/comm/root_node/etc after we return.
 */
process_t *proc_bootstrap_kernel(int pid, int perso_id) {
    process_t *proc = proc_storage_alloc();
    if (!proc) {
        return NULL;
    }

    proc->pid = pid;
    proc->ppid = 0;
    proc->perso_id = perso_id;
    proc->is_kernel_task = 1;
    proc_timers_init(proc);
    proc_resource_limits_init(proc);

    spinlock_acquire(&pid_lock);
    proc_link_locked(proc);
    spinlock_release(&pid_lock);

    return proc;
}

/*
 * KERN-06: expose the process-registry lock so a FOREACH_PROC broadcast walker
 * (sys_kill(-1)) can hold it across the walk.  proc_destroy() unlinks+frees a
 * process_t under pid_lock, so an unlocked walk can dereference freed storage.
 * A plain acquire suffices: the only concurrent freer is proc_destroy() via
 * autoreap in sched_yield(), which runs on preemption -- disabled while this
 * spinlock is held -- and via wait4() on another thread, which blocks on the
 * same lock.  No IRQ-context path takes pid_lock, so it need not be IRQ-safe.
 */
void proc_registry_lock(void)   { spinlock_acquire(&pid_lock); }
void proc_registry_unlock(void) { spinlock_release(&pid_lock); }

void proc_destroy(process_t *p) {
    if (!p) return;

    /* If this process owns the live FPU registers, relinquish ownership before
     * the struct is freed so a later #NM never fnsaves into freed storage
     * (ARCH-01). */
    fpu_forget_process(p);

    spinlock_acquire(&pid_lock);
    proc_unlink_locked(p);
    spinlock_release(&pid_lock);

    proc_storage_free(p);
}

process_t *proc_find(int pid) {
    process_t *proc;

    spinlock_acquire(&pid_lock);
    proc = proc_lookup_locked(pid);
    spinlock_release(&pid_lock);

    return proc;
}

int proc_get_last_pid(void) {
    return last_pid;
}

process_t *proc_create(int perso_id) {
    process_t *proc = proc_storage_alloc();
    if (!proc) {
        return NULL;
    }

    spinlock_acquire(&pid_lock);
    int pid = proc_alloc_pid_locked();
    if (pid < 0) {
        spinlock_release(&pid_lock);
        proc_storage_free(proc);
        return NULL;
    }
    proc->pid = pid;
    proc_link_locked(proc);
    spinlock_release(&pid_lock);

    ldt_init_process(proc);
    proc->ppid = current_process ? current_process->pid : 0;
    proc->perso_id = perso_id;
    proc->root_node = current_process ? current_process->root_node : fs_root;
    if (proc->root_node && proc->root_node != fs_root) {
        open_fs(proc->root_node, 1, 0);
    }
    proc->next_fd = 0;
    memset(proc->fd_bitmap, 0, sizeof(proc->fd_bitmap));
    memset(proc->fd_cloexec, 0, sizeof(proc->fd_cloexec));
    for (int j = 0; j < MAX_FD; j++) proc->fds[j] = 0;

    strncpy(proc->comm, "forked", AC_COMM_LEN);
    proc->comm[AC_COMM_LEN - 1] = '\0';
    proc->start_time = get_time();
    proc->uid = current_process ? current_process->uid : 0;
    proc->gid = current_process ? current_process->gid : 0;
    proc->euid = current_process ? current_process->euid : 0;
    proc->egid = current_process ? current_process->egid : 0;
    proc->suid = current_process ? current_process->suid : 0;
    proc->sgid = current_process ? current_process->sgid : 0;
    proc->umask = current_process ? current_process->umask : 022;
    /* POSIX: a child created by fork() inherits the parent's scheduling
     * policy and priority (sched_setscheduler(2)).  First process gets
     * the SCHED_OTHER/0 default. */
    proc->sched_policy      = current_process ? current_process->sched_policy : 0;
    proc->sched_rt_priority = current_process ? current_process->sched_rt_priority : 0;
    /* SCHED_SPORADIC parameters are likewise inherited across fork().  The
     * proc struct is memset(0) at allocation, so the first process starts
     * with all-zero sporadic params (sched_getparam normalizes the reported
     * sched_ss_max_repl into [1, SS_REPL_MAX] for round-trip validity). */
    if (current_process) {
        proc->sched_ss_low_priority = current_process->sched_ss_low_priority;
        proc->sched_ss_repl_period  = current_process->sched_ss_repl_period;
        proc->sched_ss_init_budget  = current_process->sched_ss_init_budget;
        proc->sched_ss_max_repl     = current_process->sched_ss_max_repl;
    }
    proc_resource_limits_init(proc);

    rusage_init(proc);
    proc_timers_init(proc);

    return proc;
}

static int proc_fork_common(process_t *parent, void *stack, int is_vfork) {
    process_t *child_proc = proc_create(parent->perso_id);
    if (!child_proc) return -ENOMEM;
    
    // Inherit process name
    strncpy(child_proc->comm, parent->comm, AC_COMM_LEN);
    child_proc->comm[AC_COMM_LEN - 1] = '\0';
    
    // Clone parent's address space with COW
    if (parent->pmap) {
        child_proc->pmap = pmap_fork(parent->pmap);
        if (!child_proc->pmap) {
            proc_destroy(child_proc);
            return -ENOMEM;
        }
    } else {
        child_proc->pmap = NULL; // Kernel process (shouldn't fork)
    }

    if (parent->vm_map && child_proc->pmap) {
        child_proc->vm_map = vm_map_fork(parent->vm_map, child_proc->pmap);
        if (!child_proc->vm_map) {
            pmap_release(child_proc->pmap);
            child_proc->pmap = NULL;
            proc_destroy(child_proc);
            return -ENOMEM;
        }
    }

    if (ldt_clone_process(child_proc, parent) != 0) {
        if (child_proc->vm_map) {
            /* vm_map_destroy() owns and frees the map's pmap (via
             * pmap_destroy(map->pmap)), so releasing the pmap again here
             * would double-free the page directory.  Tear the map down and
             * clear both pointers so the trailing proc_destroy() cannot
             * touch freed VM state. */
            vm_map_destroy(child_proc->vm_map);
            child_proc->vm_map = NULL;
            child_proc->pmap = NULL;
        } else if (child_proc->pmap) {
            /* No vm_map was created to own the pmap (e.g. a kernel process
             * that never reached vm_map_fork), so release it directly. */
            pmap_release(child_proc->pmap);
            child_proc->pmap = NULL;
        }
        proc_destroy(child_proc);
        return -ENOMEM;
    }

    // Copy cwd_node
    child_proc->cwd_node = parent->cwd_node;
    if (child_proc->cwd_node) {
        open_fs(child_proc->cwd_node, 1, 0);
    }
    strlcpy(child_proc->cwd_path, parent->cwd_path, sizeof(child_proc->cwd_path));
    child_proc->cwd_path[sizeof(child_proc->cwd_path) - 1] = '\0';
    strlcpy(child_proc->exec_path, parent->exec_path, sizeof(child_proc->exec_path));
    child_proc->exec_path[sizeof(child_proc->exec_path) - 1] = '\0';
    child_proc->cmdline_tail_len = parent->cmdline_tail_len;
    child_proc->cmdline_tail_argc = parent->cmdline_tail_argc;
    memcpy(child_proc->cmdline_tail, parent->cmdline_tail, sizeof(child_proc->cmdline_tail));
    /* The child's COW address space places argv at the same user addresses,
     * so the live arg region read by /proc/<pid>/cmdline carries over too
     * (until the child exec()s and rebuilds it). */
    child_proc->arg_start = parent->arg_start;
    child_proc->arg_end   = parent->arg_end;
    child_proc->brk_start = parent->brk_start;
    child_proc->brk = parent->brk;

    /* Inherit the user-stack bounds so demand-paged grow-down keeps
     * working in the child until it exec()s (which resets them). */
    child_proc->ustack_top   = parent->ustack_top;
    child_proc->ustack_limit = parent->ustack_limit;

    // Copy parent resources (FDs)
    child_proc->tty = parent->tty;
    child_proc->bitness = parent->bitness;
    child_proc->vfork_waiter = is_vfork ? current_thread : NULL;

    memcpy(child_proc->fd_bitmap, parent->fd_bitmap, sizeof(child_proc->fd_bitmap));
    memcpy(child_proc->fd_cloexec, parent->fd_cloexec, sizeof(child_proc->fd_cloexec));
    for(int j=0; j<MAX_FD; j++) {
        if (parent->fds[j]) {
            child_proc->fds[j] = parent->fds[j];
            child_proc->fds[j]->f_count++;
        }
    }
    
    // Copy Process Group and Session (inherit from parent)
    mutex_lock(&proctree_lock);
    child_proc->p_pgrp = parent->p_pgrp;
    
    /* 
     * Correctly adding to process group:
     * We need to add 'child_proc' to the process group's member list.
     */
    if (child_proc->p_pgrp) {
        // Add to head of pgrp list
        child_proc->p_pgrp_link = child_proc->p_pgrp->pg_members;
        child_proc->p_pgrp->pg_members = child_proc;
    } else {
        child_proc->p_pgrp_link = NULL;
    }
    mutex_unlock(&proctree_lock);
    
    // Copy Signal Actions (POSIX: inherited on fork)
    memcpy(child_proc->sig_actions, parent->sig_actions, sizeof(parent->sig_actions));
    child_proc->sig_catch = parent->sig_catch;
    child_proc->sig_ignore = parent->sig_ignore;
    memcpy(child_proc->rlimits, parent->rlimits, sizeof(parent->rlimits));
    /* Resource limits are inherited across fork(); memory locks are not, so
     * the child starts with no mlockall() in effect. */
    child_proc->rlim_memlock_cur = parent->rlim_memlock_cur;
    child_proc->rlim_memlock_max = parent->rlim_memlock_max;
    child_proc->rlim_as_cur      = parent->rlim_as_cur;
    child_proc->rlim_as_max      = parent->rlim_as_max;
    child_proc->mlockall_flags   = 0;
    child_proc->umask = parent->umask;
    /* Supplementary group list inherits across fork. */
    memcpy(child_proc->supp_groups, parent->supp_groups,
           sizeof(parent->supp_groups));
    child_proc->n_supp_groups = parent->n_supp_groups;
    
    // Copy limits, etc. if implemented
    
    // Create Thread for child
    
    /* 
     * Add child to parent's child list BEFORE making it schedulable.
     * This ensures wait() sees a fully linked child and the child
     * cannot run before it appears in the process tree.
     */
    proc_add_child(parent, child_proc);

    int fork_result = sched_fork_thread(child_proc, stack);
    if (fork_result < 0) {
        proc_remove_child(parent, child_proc);

        if (child_proc->p_pgrp) {
            pgrp_remove_proc(child_proc);
        }

        for (int j = 0; j < MAX_FD; j++) {
            if (child_proc->fds[j]) {
                child_proc->fds[j]->f_count--;
                child_proc->fds[j] = NULL;
            }
        }

        if (child_proc->cwd_node) {
            close_fs(child_proc->cwd_node);
            child_proc->cwd_node = NULL;
        }

        ldt_free_process(child_proc);

        if (child_proc->vm_map) {
            vm_map_destroy(child_proc->vm_map);
            child_proc->vm_map = NULL;
        }
        if (child_proc->pmap) {
            pmap_release(child_proc->pmap);
            child_proc->pmap = NULL;
        }

        proc_destroy(child_proc);
        return fork_result;
    }

    /* Diverge parent/child CSPRNG state immediately after fork */
    random_reseed_on_fork(fork_result);

    return fork_result;
}

static void proc_sysvipc_exit(process_t *proc) {
    /*
     * Dedicated teardown hook for future System V IPC ownership tracking.
     *
     * The current kernel does not implement in-kernel SysV shared memory,
     * semaphore undo state, or message-queue ownership, so process exit has
     * nothing concrete to detach yet. Keep the phase explicit so later IPC
     * work lands in one place instead of reopening proc_exit sequencing.
     */
    (void)proc;
}

static void proc_posixipc_exit(process_t *proc) {
    /*
     * Dedicated teardown hook for future POSIX semaphore/shared-memory
     * ownership tracking. No kernel-managed POSIX IPC namespace exists yet,
     * so exit cleanup is intentionally a no-op today.
     */
    (void)proc;
}

int proc_fork(process_t *parent, void *stack) {
    return proc_fork_common(parent, stack, 0);
}

int proc_vfork(process_t *parent, void *stack) {
    return proc_fork_common(parent, stack, 1);
}

void proc_add_child(process_t *parent, process_t *child) {
    if (parent && PROC_DEBUG_ENABLED()) {
        PROC_DEBUG("proc: add_child parent=%d child=%d before=%p\n",
                   parent->pid, child ? child->pid : -1,
                   parent->p_children);
    }
    mutex_lock(&proctree_lock);
    child->p_parent = parent;
    child->p_sibling = parent->p_children;
    parent->p_children = child;
    if (PROC_DEBUG_ENABLED()) {
        PROC_DEBUG("proc: add_child parent=%d child=%d after=%p sibling=%p\n",
                   parent ? parent->pid : -1,
                   child ? child->pid : -1,
                   parent ? parent->p_children : NULL,
                   child ? child->p_sibling : NULL);
    }
    mutex_unlock(&proctree_lock);
}

void proc_remove_child(process_t *parent, process_t *child) {
    if (!parent || !child) return;
    if (PROC_DEBUG_ENABLED()) {
        PROC_DEBUG("proc: remove_child parent=%d child=%d state=%u flags=%#x\n",
                   parent->pid, child->pid, child->state,
                   child->p_flag);
    }
    mutex_lock(&proctree_lock);
    if (parent->p_children == child) {
        parent->p_children = child->p_sibling;
    } else {
        process_t *prev = parent->p_children;
        while (prev && prev->p_sibling != child) prev = prev->p_sibling;
        if (prev) prev->p_sibling = child->p_sibling;
    }
    child->p_sibling = NULL;
    // child->p_parent = NULL; // Optional, called often reassigns
    mutex_unlock(&proctree_lock);
}

// Shim for syscalls
int sched_fork_process(process_t *parent, void *stack) {
    return proc_fork(parent, stack);
}

int proc_begin_vfork(process_t *child) {
    if (current_process && PROC_DEBUG_ENABLED()) {
        PROC_DEBUG("proc: begin_vfork parent=%d child=%p childpid=%d waiter=%p current=%p\n",
                   current_process->pid,
                   child,
                   child ? child->pid : -1,
                   child ? child->vfork_waiter : NULL,
                   current_thread);
    }
    if (!child || child->vfork_waiter != current_thread) {
        return 0;
    }

    sched_sleep(child);
    return 0;
}

void proc_vfork_done(process_t *child) {
    if (child && PROC_DEBUG_ENABLED()) {
        PROC_DEBUG("proc: vfork_done child=%d waiter=%p\n",
                   child->pid, child->vfork_waiter);
    }
    if (!child || !child->vfork_waiter) {
        return;
    }

    child->vfork_waiter = NULL;
    sched_wakeup(child);
}

int sched_spawn_kernel_process(void (*entry)(void*), void *arg) {


    // 1. Create Process
    process_t *child = proc_create(PERS_NATIVE);
    if (!child) return -1;
    
    // 2. Set Name
    strncpy(child->comm, "(kinit)", AC_COMM_LEN);
    child->comm[AC_COMM_LEN - 1] = '\0';

    /* 3. Allocate kernel stack — 16 KiB (4 contiguous PMM blocks).
     * 8 KiB overflowed: the network TX path nests three ~1.5 KiB
     * stack buffers (tcp_xmit_raw buf[1480] -> ip4_output pkt[1600]
     * -> eth_send frame[1614]) on top of the syscall call chain,
     * ~8 KiB total — it ran off the end and corrupted adjacent
     * memory + the saved trap frame. */
    void *stack = pmm_alloc_contiguous(4);
    if (!stack) {
        return -1;
    }
    void *stack_top = (uint8_t*)stack + 16384;

    // 4. Create Thread
    thread_t *t = sched_create_thread(child, entry, stack_top, arg);
    if (!t) {
        pmm_free_contiguous(stack, 4);
        return -1;
    }

    t->kstack_base = (uintptr_t)stack;
    t->kstack_units = 4;
    t->kstack_type = THREAD_KSTACK_PMM_CONTIG;
    t->kstack_owned = 1;

    return t->tid;
}

// Close all FDs for a process
void fd_close_all(process_t *p) {
    if (!p) return;
    for (int i = 0; i < MAX_FD; i++) {
        if (p->fds[i]) {
            // Check refs? sys_close does that.
            // But we need to call file_close / decref logic.
            // Since we don't have direct access to sys_close here (it's in syscall.c),
            // we should duplicate the decrement logic or expose a helper.
            // syscall.c 'sys_close' calls 'close_fs' and 'file_free'.
            // We'll declare an external helper or move implementation.
            // For now, let's assume we can call an external 'file_close(file_t*)'
            file_close_ptr(p->fds[i]);
            proc_clear_fd(p, i);
        }
    }
}

static int proc_status_flags_from_file(const file_t *f) {
    int flags;

    if (!f) {
        return 0;
    }

    if ((f->f_flag & FREAD) && (f->f_flag & FWRITE)) {
        flags = O_RDWR;
    } else if (f->f_flag & FWRITE) {
        flags = O_WRONLY;
    } else {
        flags = O_RDONLY;
    }

    if (f->f_flag & FAPPEND) {
        flags |= O_APPEND;
    }
    if (f->f_flag & FNONBLOCK) {
        flags |= O_NONBLOCK;
    }

    return flags;
}

static void proc_apply_status_flags(file_t *f, int flags) {
    if (!f) {
        return;
    }

    f->f_flag &= ~(FAPPEND | FNONBLOCK);
    if (flags & O_APPEND) {
        f->f_flag |= FAPPEND;
    }
    if (flags & O_NONBLOCK) {
        f->f_flag |= FNONBLOCK;
    }

    /* Pipe endpoints carry their own ep->nonblock flag because the
     * pipe_read / pipe_write callbacks only see the fs_node, not the
     * file_t.  Mirror the file-level flag down to the endpoint so
     * fcntl(F_SETFL, O_NONBLOCK) actually changes pipe behaviour —
     * without this, bsdtar's select-multiplex through gzip
     * deadlocks because every read on the pipe still blocks. */
    if (f->f_data) {

        fs_node_t *node = (fs_node_t *)f->f_data;
        int nb = (flags & O_NONBLOCK) ? 1 : 0;
        if ((node->flags & 0x7) == FS_PIPE) {
            (void)pipe_set_nonblock(node, nb);
        }
        /* A pty master's read callback only sees the fs_node, so the
         * non-blocking flag must be mirrored down to the pair.  The
         * call is a no-op for any node that is not a pty master. */
        (void)pty_set_nonblock(node, nb);
    }
}

int proc_alloc_fd_from(process_t *p, int start) {
    int fd;

    if (!p) {
        return -1;
    }
    if (start < 0) {
        return -EINVAL;
    }
    if (start >= MAX_FD) {
        return -1;
    }

    for (fd = start; fd < MAX_FD; fd++) {
        if (!fdset_test(p->fd_bitmap, fd)) {
            fdset_set(p->fd_bitmap, fd);
            fdset_clear(p->fd_cloexec, fd);
            p->next_fd = (fd + 1) % MAX_FD;
            return fd;
        }
    }

    return -1;
}

int proc_alloc_fd(process_t *p) {
    if (!p) return -1;

    int start = p->next_fd;
    if (start < 0 || start >= MAX_FD) start = 0;

    /* Word-scan the allocated-fd bitmap from the next_fd hint, wrapping
     * once.  Each 32-fd word with a free bit is resolved with ctz on
     * its complement. */
    for (int w = 0; w < FD_BITMAP_WORDS; w++) {
        int word = ((start >> 5) + w) % FD_BITMAP_WORDS;
        uint32_t free_bits = ~p->fd_bitmap[word];
        if (w == 0) {
            /* In the starting word, ignore bits below `start`. */
            int off = start & 31;
            free_bits &= ~((1u << off) - 1u);
        }
        if (free_bits == 0) continue;
        int fd = word * 32 + __builtin_ctz(free_bits);
        if (fd >= MAX_FD) continue;
        fdset_set(p->fd_bitmap, fd);
        fdset_clear(p->fd_cloexec, fd);
        p->next_fd = (fd + 1) % MAX_FD;
        return fd;
    }
    /* The starting word's low bits (below `start`) weren't scanned by
     * the wrap above; do a final full pass to be safe. */
    for (int fd = 0; fd < MAX_FD; fd++) {
        if (!fdset_test(p->fd_bitmap, fd)) {
            fdset_set(p->fd_bitmap, fd);
            fdset_clear(p->fd_cloexec, fd);
            p->next_fd = (fd + 1) % MAX_FD;
            return fd;
        }
    }
    return -1;
}

void proc_set_fd(process_t *p, int fd, file_t *f) {
    if (!p || fd < 0 || fd >= MAX_FD) return;
    p->fds[fd] = f;
    if (f) {
        fdset_set(p->fd_bitmap, fd);
    } else {
        fdset_clear(p->fd_bitmap, fd);
    }
}

void proc_clear_fd(process_t *p, int fd) {
    if (!p || fd < 0 || fd >= MAX_FD) return;
    fdset_clear(p->fd_cloexec, fd);
    proc_set_fd(p, fd, NULL);
}

/* ------------------------------------------------------------------
 * POSIX advisory record locks (fcntl F_GETLK / F_SETLK / F_SETLKW).
 *
 * Locks hang off the open file description (struct file.f_advlock), so
 * they are shared by dup() and fork() (which share the file_t) yet remain
 * owned by a specific pid.  A forked child therefore sees the parent's
 * lock as belonging to ANOTHER owner: locks are NOT inherited across
 * fork() even though the mapping/fd is (OPTS fork/11-1).  The list is
 * freed with the last reference to the description (file_free() ->
 * advlock_release_file()).  Enforcement is scoped to a single shared
 * description, so two independent open()s of the same file keep separate
 * lists and never contend — preserving substrate's historical
 * single-writer no-op behaviour for pwdb/sqlite/... that never share a
 * locked fd across processes.
 *
 * kmalloc()/kfree() must not run under the spinlock, so nodes are
 * allocated before it is taken and freed after it is released.
 * ------------------------------------------------------------------ */
struct advlock {
    off_t          start;
    off_t          end;        /* exclusive; ADVLOCK_EOF == to end of file */
    short          type;       /* F_RDLCK / F_WRLCK */
    int            owner;      /* owning pid */
    struct advlock *next;
};
#define ADVLOCK_EOF ((off_t)0x7fffffffffffffffLL)

/* Mirrors the userspace <fcntl.h> struct flock (i386 layout: int64 off_t
 * is 4-byte aligned, so the struct is 24 bytes with no trailing pad). */
struct kflock {
    int16_t l_type;
    int16_t l_whence;
    int64_t l_start;
    int64_t l_len;
    int32_t l_pid;
};

static spinlock_t advlock_lock = SPINLOCK_INIT("fcntl_advlock");

static int advlock_overlap(off_t s1, off_t e1, off_t s2, off_t e2) {
    return s1 < e2 && s2 < e1;
}
static int advlock_conflict(short a, short b) {
    return a == F_WRLCK || b == F_WRLCK;
}

/* Resolve a struct flock's l_whence/l_start/l_len to an absolute [start,end). */
static int advlock_range(file_t *f, const struct kflock *fl,
                         off_t *out_start, off_t *out_end) {
    off_t base;
    switch (fl->l_whence) {
    case 0: base = 0; break;                                   /* SEEK_SET */
    case 1: base = f->f_offset; break;                         /* SEEK_CUR */
    case 2:                                                    /* SEEK_END */
        /* Only a regular file has a meaningful size to seek relative to; a
         * socket / pipe / device f_data is not a length-bearing fs_node, so
         * SEEK_END against one is EINVAL rather than a bogus base offset. */
        if (!f->f_data || (((fs_node_t *)f->f_data)->flags & 0x07) != FS_FILE)
            return -EINVAL;
        base = ((fs_node_t *)f->f_data)->length;
        break;
    default: return -EINVAL;
    }
    /* Reject offset arithmetic that overflows off_t (EINVAL) instead of
     * wrapping into a bogus [start,end) that would mis-order the lock list. */
    off_t start;
    if (__builtin_add_overflow(base, fl->l_start, &start))
        return -EINVAL;
    off_t end;
    if (fl->l_len == 0) {
        end = ADVLOCK_EOF;
    } else if (fl->l_len > 0) {
        if (__builtin_add_overflow(start, fl->l_len, &end))
            return -EINVAL;
    } else {                       /* negative len: region [start+len, start) */
        end = start;
        if (__builtin_add_overflow(start, fl->l_len, &start))
            return -EINVAL;
    }
    if (start < 0) return -EINVAL;
    *out_start = start;
    *out_end = end;
    return 0;
}

/*
 * Clip this owner's lock coverage over [start,end).  A lock entirely inside
 * the range is unlinked and returned on the removed chain (freed by the caller
 * outside the lock).  A lock that extends past the range on one side is
 * trimmed in place (type preserved); a lock that strictly contains [start,end)
 * is split into a leading [l->start,start) fragment (the reused node) and a
 * trailing [end,l->end) fragment (the pre-allocated *spare, set NULL once
 * consumed).  This makes a partial F_UNLCK — or a same-owner F_SETLK that
 * replaces only part of a bigger lock — preserve the surrounding regions
 * instead of destroying the whole overlapping lock wholesale.
 */
static struct advlock *advlock_clip_owner_range(file_t *f, int owner,
                                                off_t start, off_t end,
                                                struct advlock **spare) {
    struct advlock *removed = NULL;
    struct advlock **pp = (struct advlock **)&f->f_advlock;
    while (*pp) {
        struct advlock *l = *pp;
        if (l->owner != owner || !advlock_overlap(l->start, l->end, start, end)) {
            pp = &l->next;
            continue;
        }
        int keep_lead  = l->start < start;   /* preserve [l->start, start) */
        int keep_trail = l->end   > end;     /* preserve [end,      l->end) */
        if (keep_lead && keep_trail) {
            /* [start,end) lies strictly inside l: split into leading (reuse l)
             * and trailing (spare) fragments, both keeping l's type. */
            struct advlock *t = *spare;
            if (t) {
                *spare   = NULL;
                t->start = end;
                t->end   = l->end;
                t->type  = l->type;
                t->owner = owner;
                t->next  = l->next;
                l->next  = t;
                l->end   = start;
                pp = &t->next;
            } else {
                /* No spare (the caller pre-allocates one, so unreachable in
                 * practice): degrade to keeping only the leading fragment. */
                l->end = start;
                pp = &l->next;
            }
        } else if (keep_lead) {
            l->end = start;          /* trim the overlapping tail */
            pp = &l->next;
        } else if (keep_trail) {
            l->start = end;          /* trim the overlapping head */
            pp = &l->next;
        } else {
            /* Fully covered: unlink + collect for freeing outside the lock. */
            *pp = l->next;
            l->next = removed;
            removed = l;
        }
    }
    return removed;
}

static void advlock_free_chain(struct advlock *l) {
    while (l) {
        struct advlock *n = l->next;
        kfree(l, sizeof(*l));
        l = n;
    }
}

/* Called from file_free() when the last reference to an open file
 * description is dropped: release every record lock it carries. */
void advlock_release_file(file_t *f) {
    if (!f) return;
    spinlock_acquire(&advlock_lock);
    struct advlock *l = (struct advlock *)f->f_advlock;
    f->f_advlock = NULL;
    spinlock_release(&advlock_lock);
    advlock_free_chain(l);
}

/*
 * Release just `owner`'s record locks on this open file description, leaving
 * any held by other owners intact.  POSIX requires a process's locks on a
 * file to be dropped when it closes a descriptor for that file OR when it
 * exits — NOT deferred to the last close of a description that fork()/dup()
 * left shared.  advlock_release_file() only fires at the final f_count==0
 * drop, so without this a process that closes its fd (or exits) while a
 * fork-shared referrer still holds the description would leave its locks
 * lingering under its (soon-reused) pid, wrongly blocking other lockers.
 * Called from the close path (even when f_count > 0) and, via fd_close_all,
 * for every descriptor an exiting process still holds open.
 */
void advlock_release_by_owner(file_t *f, int owner) {
    if (!f) return;
    struct advlock *removed = NULL;
    spinlock_acquire(&advlock_lock);
    struct advlock **pp = (struct advlock **)&f->f_advlock;
    while (*pp) {
        struct advlock *l = *pp;
        if (l->owner == owner) {
            *pp = l->next;
            l->next = removed;
            removed = l;
        } else {
            pp = &l->next;
        }
    }
    spinlock_release(&advlock_lock);
    advlock_free_chain(removed);
}

/* fcntl F_GETLK: report a conflicting lock owned by another process, or
 * F_UNLCK if the requested region is grantable. */
static int advlock_getlk(process_t *p, file_t *f, int arg) {
    struct kflock fl;
    if (!arg) return -EFAULT;
    if (copyin((void *)(uintptr_t)(unsigned)arg, &fl, sizeof(fl)) != 0)
        return -EFAULT;
    if (fl.l_type != F_RDLCK && fl.l_type != F_WRLCK) return -EINVAL;
    off_t s, e;
    int r = advlock_range(f, &fl, &s, &e);
    if (r) return r;

    spinlock_acquire(&advlock_lock);
    struct advlock *hit = NULL;
    for (struct advlock *l = (struct advlock *)f->f_advlock; l; l = l->next) {
        if (l->owner != p->pid &&
            advlock_overlap(l->start, l->end, s, e) &&
            advlock_conflict(fl.l_type, l->type)) {
            hit = l;
            break;
        }
    }
    if (hit) {
        fl.l_type   = hit->type;
        fl.l_whence = 0;   /* SEEK_SET */
        fl.l_start  = hit->start;
        fl.l_len    = (hit->end == ADVLOCK_EOF) ? 0 : (hit->end - hit->start);
        fl.l_pid    = hit->owner;
    } else {
        fl.l_type = F_UNLCK;
    }
    spinlock_release(&advlock_lock);

    if (copyout(&fl, (void *)(uintptr_t)(unsigned)arg, sizeof(fl)) != 0)
        return -EFAULT;
    return 0;
}

/* fcntl F_SETLK / F_SETLKW. */
static int advlock_setlk(process_t *p, file_t *f, int arg) {
    struct kflock fl;
    if (!arg) return -EFAULT;
    if (copyin((void *)(uintptr_t)(unsigned)arg, &fl, sizeof(fl)) != 0)
        return -EFAULT;
    if (fl.l_type != F_RDLCK && fl.l_type != F_WRLCK && fl.l_type != F_UNLCK)
        return -EINVAL;
    off_t s, e;
    int r = advlock_range(f, &fl, &s, &e);
    if (r) return r;

    /* Allocate the nodes up front so kmalloc never runs under the spinlock
     * (it may sleep / take other locks):
     *   nl    — the replacement lock to insert (F_SETLK/F_SETLKW only).
     *   split — a spare fragment consumed only when clipping must cut an
     *           existing lock that strictly contains [s,e) into two pieces. */
    struct advlock *nl = NULL;
    if (fl.l_type != F_UNLCK) {
        nl = kmalloc(sizeof(*nl));
        if (!nl) return -ENOMEM;
    }
    struct advlock *split = kmalloc(sizeof(*split));
    if (!split) {
        kfree(nl, sizeof(*nl));      /* kfree(NULL) is a no-op */
        return -ENOMEM;
    }

    spinlock_acquire(&advlock_lock);
    if (fl.l_type != F_UNLCK) {
        for (struct advlock *l = (struct advlock *)f->f_advlock; l; l = l->next) {
            if (l->owner != p->pid &&
                advlock_overlap(l->start, l->end, s, e) &&
                advlock_conflict(fl.l_type, l->type)) {
                spinlock_release(&advlock_lock);
                kfree(nl, sizeof(*nl));
                kfree(split, sizeof(*split));
                /* F_SETLKW blocking is not implemented; a conflict is only
                 * reachable when processes share the SAME open file
                 * description (fork/dup) — reported as EAGAIN either way. */
                return -EAGAIN;
            }
        }
    }
    /* Clip this owner's coverage of [s,e): partial overlaps are trimmed/split
     * (surrounding fragments preserved), fully-covered locks are returned for
     * freeing outside the lock.  For F_UNLCK this is the whole operation. */
    struct advlock *removed = advlock_clip_owner_range(f, p->pid, s, e, &split);
    if (nl) {
        nl->start = s;
        nl->end   = e;
        nl->type  = fl.l_type;
        nl->owner = p->pid;
        nl->next  = (struct advlock *)f->f_advlock;
        f->f_advlock = nl;
    }
    spinlock_release(&advlock_lock);
    advlock_free_chain(removed);
    if (split) kfree(split, sizeof(*split));   /* spare fragment unused */
    return 0;
}

int proc_fcntl(process_t *p, int fd, int cmd, int arg) {
    file_t *f;
    int newfd;

    if (!p || fd < 0 || fd >= MAX_FD) {
        return -EBADF;
    }

    f = p->fds[fd];
    if (!f) {
        return -EBADF;
    }

    switch (cmd) {
    case F_DUPFD:
        if (arg < 0) {
            return -EINVAL;
        }
        if (arg >= MAX_FD) {
            return -EINVAL;
        }
        newfd = proc_alloc_fd_from(p, arg);
        if (newfd < 0) {
            return -EMFILE;
        }
        proc_set_fd(p, newfd, f);
        fdset_clear(p->fd_cloexec, newfd);
        f->f_count++;
        return newfd;
    case F_GETFD:
        return fdset_test(p->fd_cloexec, fd) ? FD_CLOEXEC : 0;
    case F_SETFD:
        if (arg & FD_CLOEXEC) {
            fdset_set(p->fd_cloexec, fd);
        } else {
            fdset_clear(p->fd_cloexec, fd);
        }
        return 0;
    case F_GETFL:
        return proc_status_flags_from_file(f);
    case F_SETFL:
        proc_apply_status_flags(f, arg);
        return 0;
    case F_GETLK:
        return advlock_getlk(p, f, arg);
    case F_SETLK:
    case F_SETLKW:
        return advlock_setlk(p, f, arg);
    case F_SETOWN:
        /*
         * Set the pid/pgrp that receives SIGIO/SIGURG for this fd.
         * substrate doesn't deliver SIGIO yet, so this is an
         * accept-only no-op.  It MUST succeed, though: nginx's
         * ngx_spawn_process() does fcntl(channel, F_SETOWN, pid) right
         * after ioctl(FIOASYNC) and aborts the entire worker spawn
         * (-> NGX_INVALID_PID) on any error.  Consumers that need the
         * notification also poll the fd in their event loop, so a
         * missing SIGIO doesn't break them.
         */
        return 0;
    case F_GETOWN:
        /* No SIGIO owner tracking — report "none". */
        return 0;
    default:
        return -EINVAL;
    }
}

/*
 * FIONBIO — toggle O_NONBLOCK on an open descriptor.  Defined to be
 * equivalent to fcntl(fd, F_SETFL) flipping O_NONBLOCK, so it routes
 * through the same status-flag path (incl. the pipe-endpoint mirror)
 * for identical behaviour.  Called by the generic ioctl dispatch.
 */
int proc_fd_set_nonblock(int fd, int on) {
    process_t *p = current_process;
    file_t *f;
    int flags;

    if (!p || fd < 0 || fd >= MAX_FD) {
        return -EBADF;
    }
    f = p->fds[fd];
    if (!f) {
        return -EBADF;
    }

    flags = proc_status_flags_from_file(f);
    if (on) {
        flags |= O_NONBLOCK;
    } else {
        flags &= ~O_NONBLOCK;
    }
    proc_apply_status_flags(f, flags);
    return 0;
}

void proc_close_cloexec(process_t *p) {
    int fd;

    if (!p) {
        return;
    }

    for (fd = 0; fd < MAX_FD; fd++) {
        if (!fdset_test(p->fd_cloexec, fd)) {
            continue;
        }
        if (p->fds[fd]) {
            file_close_ptr(p->fds[fd]);
        }
        proc_clear_fd(p, fd);
        if (fd < p->next_fd) {
            p->next_fd = fd;
        }
    }
}

// Reparent children to init
void proc_reparent_children(process_t *p) {
    process_t *init;
    process_t *child, *next;
    
    /*
     * Acquire proctree_lock to protect the process hierarchy.
     * This prevents races with other processes modifying the tree.
     */
    mutex_lock(&proctree_lock);
    
    if (!p->p_children) {
        mutex_unlock(&proctree_lock);
        return;
    }
    
    init = proc_find(1);
    if (!init || init->state == SDYING || init->state == SZOMB) {
        init = kernel_process;
    }
    
    child = p->p_children;
    while (child) {
        next = child->p_sibling;
        
        // Remove from p's list (conceptually done by iterating)
        child->p_parent = init;
        /* Keep the integer ppid in sync with the new parent so getppid(2)
         * on an orphan returns init's pid (1), not the dead parent's. */
        child->ppid = init->pid;

        // Add to init's list
        child->p_sibling = init->p_children;
        init->p_children = child;
        
        child = next;
    }
    p->p_children = NULL;
    
    /*
     * Unlock proctree_lock before waking up init.
     * This avoids holding the lock while potentially causing
     * a context switch.
     */
    mutex_unlock(&proctree_lock);
    
    // Wakeup init if it was waiting for children
    sched_wakeup(&init->p_children);
}

static int proc_threads_all_zombie(process_t *proc, thread_t *skip_thread) {
    FOREACH_THREAD(thread) {
        if (thread->proc != proc || thread == skip_thread) {
            continue;
        }
        if (thread->state != THREAD_ZOMBIE) {
            return 0;
        }
    }
    return 1;
}

static void proc_release_zombie_resources(process_t *proc) {
    if (!proc) {
        return;
    }

    /*
     * Release the heap (brk) commit reservation.  Heap pages are mapped
     * directly into the pmap, not via vm_map entries, so vm_map_destroy
     * does not account for them -- uncharge here so exit balances the
     * global commit counter exactly.  (Anonymous-mmap reservations are
     * uncharged by vm_map_destroy via VME_COMMITTED.)
     */
    if (proc->brk_committed) {
        vm_commit_uncharge(proc->brk_committed);
        proc->brk_committed = 0;
    }

    if (proc->vm_map) {
        vm_map_destroy(proc->vm_map);
        proc->vm_map = NULL;
    } else if (proc->pmap && proc->pmap != pmap_kernel()) {
        pmap_release(proc->pmap);
    }

    if (proc->p_pgrp) {
        pgrp_remove_proc(proc);
    }

    ldt_free_process(proc);

    proc->pmap = pmap_kernel();
}

void proc_reap_autoreap_zombies(void) {
    /*
     * INVARIANT: this runs from sched_yield() with interrupts ENABLED (the
     * call sits BEFORE sched_yield()'s intr_disable) and holding no lock.
     *
     * The old body walked `allproc` with a cached `next` cursor and freed the
     * proc mid-walk.  A timer preempt anywhere in that walk (preempt_count==0,
     * so a tick yields) — or a voluntary sleep inside the sleepable teardown
     * below (vm_map_destroy -> shmfs device-pager dtor takes a mutex) — lets
     * another thread run and reap/free a proc: either a concurrent wait4()
     * freeing the very proc this walk cached in `proc`/`next`, or this function
     * re-entered on the nested sched_yield() double-reaping a proc still linked
     * mid-teardown (double vm_map_destroy -> double free).  Both dangle a
     * pointer and silently triple-fault under mass fork/exit/reap — OPTS
     * shm_open/23-1 forks ~1000 children, so init reaps a storm of zombies
     * while the idle loop walks `allproc` on every yield.
     *
     * Fix: find ONE victim under pid_lock (preemption-safe — no tick can land
     * mid-scan and no other CPU can mutate allproc), UNLINK it from allproc +
     * pid_hash before the sleepable teardown so no re-entered/concurrent reaper
     * can find or free it, then re-scan from the head so no list cursor is ever
     * held across a preempt point.  A one-shot guard keeps a mid-teardown
     * preempt from recursively re-entering (bounding kernel-stack depth); the
     * outer loop still reaps everything the skipped nested call would have.
     */
    if (autoreap_pending == 0) {
        return;   /* fast path: nothing to reap, don't touch pid_lock */
    }

    static volatile int reaping;
    if (__sync_lock_test_and_set(&reaping, 1)) {
        return;
    }

    for (;;) {
        process_t *victim = NULL;

        spinlock_acquire(&pid_lock);
        for (process_t *proc = allproc; proc; proc = proc->p_allproc_next) {
            if (proc->pid <= 0 || proc->state != SZOMB ||
                !(proc->p_flag & P_AUTOREAP)) {
                continue;
            }
            if (current_thread && current_thread->proc == proc) {
                continue;
            }
            if (!proc_threads_all_zombie(proc, NULL)) {
                continue;
            }
            victim = proc;
            proc_unlink_locked(victim);   /* out of allproc + pid_hash now */
            break;
        }
        spinlock_release(&pid_lock);

        if (!victim) {
            break;
        }
        __sync_fetch_and_sub(&autoreap_pending, 1);

        /* victim is unlinked and owned solely by us: the teardown may sleep,
         * but nothing can rediscover victim, and the guard blocks re-entry. */
        proc_release_zombie_resources(victim);
        sched_reap_process_threads(victim);
        proc_storage_free(victim);
    }

    __sync_lock_release(&reaping);
}

void proc_exit(int code) {
    if (current_process->pid == 1) {
        kprint("Warning: Init process exited. System Halted (idle).\n");
        for (;;) {
            proc_idle_wait();
        }
    }

    /* Reverse this process's SEM_UNDO adjustments before it disappears. */
    {
        sem_proc_cleanup(current_process->pid);
    }

    /* Detach every System V shared-memory segment this process still holds
     * (frees a segment's backing pages if this was its last attach and it was
     * marked IPC_RMID).  Runs while vm_map is still live so vm_map_remove can
     * tear down the mappings. */
    {
        shm_proc_cleanup(current_process->pid);
    }

    /* Drop any POSIX named / process-shared semaphore descriptors this
     * process still holds open (frees the ksem object if this was its last
     * descriptor and it was unlinked or anonymous). */
    ksem_proc_cleanup(current_process->pid);
    /* Close any POSIX message-queue descriptors this process still holds and
     * drop any mq_notify registration it owned. */
    {
        mq_proc_cleanup(current_process->pid);
    }

    /* Release any VT we put into KD_GRAPHICS (X server crash, etc.)
     * before tearing down fds.  Without this, a SEGV'd X server leaves
     * the framebuffer wedged in graphics mode and the keyboard in
     * K_RAW even though all sibling VTs are alive. */
    {
        vt_release_graphics_on_exit((void *)current_process);
    }

    /* Leak instrumentation.  When booted with `debug=vm_leak` print
     * a one-line snapshot at every proc_exit so we can watch the
     * vnode-pager / vm_map / vnode-stat counters drift relative to
     * exec/exit count.  Healthy steady-state: alloc - dealloc == 0. */
    if (cmdline_debug_enabled("vm_leak")) {

        struct bio_stats bs;
        bio_get_stats(&bs);

        kprintf("vm_leak[pid=%d exit=%d]: pager A=%lu D=%lu (live=%ld) "
                "map_destroy=%lu (ents=%lu) "
                "fs_open=%lu fs_close=%lu (drift=%ld) "
                "pmap C=%llu D=%llu (live=%lld) "
                "nc=%d (E=%lu V=%lu P=%lu) "
                "bio nbuf=%u bytes=%llu hits=%llu miss=%llu reclaim=%llu "
                "[L=%u C=%u D=%u E=%u] "
                "ext2 H=%llu N=%llu F=%llu (pin=%llu lock=%llu) "
                "fd C=%llu Dh=%llu Wf=%llu Wm=%llu (mal=%llu blk0=%llu) "
                "rootlost=%llu\n",
                current_process->pid, code,
                vm_pager_vnode_alloc_count,
                vm_pager_vnode_dealloc_count,
                (long)(vm_pager_vnode_alloc_count - vm_pager_vnode_dealloc_count),
                vm_map_destroy_count, vm_map_destroy_entries,
                fs_open_count, fs_close_count,
                (long)(fs_open_count - fs_close_count),
                (unsigned long long)pmap_create_calls,
                (unsigned long long)pmap_destroy_calls,
                (long long)(pmap_create_calls - pmap_destroy_calls),
                vfs_cache_count,
                namecache_enter_count, namecache_evict_count,
                namecache_purge_count,
                bs.nbuf,
                (unsigned long long)bs.resident_bytes,
                (unsigned long long)bs.hits,
                (unsigned long long)bs.misses,
                (unsigned long long)bs.reclaims,
                bs.q_locked, bs.q_clean, bs.q_dirty, bs.q_empty,
                (unsigned long long)ext2_alloc_node_hits,
                (unsigned long long)ext2_alloc_node_new,
                (unsigned long long)ext2_alloc_node_fail,
                (unsigned long long)ext2_alloc_node_fail_pinned,
                (unsigned long long)ext2_alloc_node_fail_locked,
                (unsigned long long)ext2_finddir_calls,
                (unsigned long long)ext2_finddir_dcache_hit,
                (unsigned long long)ext2_finddir_walk_found,
                (unsigned long long)ext2_finddir_walk_missing,
                (unsigned long long)ext2_finddir_break_recv_malformed,
                (unsigned long long)ext2_finddir_break_block0,
                (unsigned long long)ext2_root_pin_lost);

        /* Census of the two resources the counters above don't track:
         * live thread_t (walk allthread, by state) and live process_t
         * (walk allproc, by state).  A monotonically climbing total is a
         * thread_t / process_t leak — and a growing allthread makes
         * sched_yield O(n) per pick, so a leak here shows as "slows". */
        {
            unsigned t_total = 0, t_zomb = 0, t_blocked = 0;
            FOREACH_THREAD(th) {
                t_total++;
                if (th->state == THREAD_ZOMBIE)  t_zomb++;
                else if (th->state == THREAD_BLOCKED) t_blocked++;
            }
            unsigned p_total = 0, p_zomb = 0;
            for (process_t *pp = proc_first(); pp; pp = proc_next(pp)) {
                p_total++;
                if (pp->state == SZOMB || pp->state == SDYING) p_zomb++;
            }
            kprintf("census[pid=%d]: threads=%u (zomb=%u blk=%u) procs=%u (zomb=%u)\n",
                    current_process->pid, t_total, t_zomb, t_blocked,
                    p_total, p_zomb);
        }
    }

    /* Per-syscall cost histogram dump for the just-exited process.
     * Counters are accumulated across the whole boot (per personality),
     * so for clean per-process windows pass reset=1 here. */
    if (cmdline_debug_enabled("syscall_stats")) {
        kprintf("=== syscall_stats[pid=%d exit=%d] ===\n",
                current_process->pid, code);
        syscall_stats_dump(1);
        kprintf("=== end syscall_stats ===\n");
    }

    proc_vfork_done(current_process);
    
    // 1. Set State
    current_process->state = SDYING;
    current_process->exit_code = code;
    if ((current_process->p_flag & P_SIGEXIT) == 0) {
        current_process->p_flag &= (uint16_t)~P_SIGEXIT;
    }
    
    // 1b. Thread Cleanup (Robust Futexes & Pending Signals)
    FOREACH_THREAD(thread) {
        if (thread->proc != current_process) continue;

        /* Cleanup robust futexes for this thread */
        futex_thread_exit(thread);

        if (mutex_release_owned_by_thread(thread) > 0) {
            kprint("proc_exit: force-releasing mutexes held by exiting thread.\n");
        }

        /* Remove sleepers from sleep queues before they become zombies. */
        sleepq_remove_thread(thread);

        /* Clear pending signals - process is dying */
        thread->sig_pending = 0;

        /* Mark as zombie to stop execution (if not current thread) */
        if (thread != current_thread) {
            thread->state = THREAD_ZOMBIE;
        }
    }

    acct_process(code);
    
    // 2. Resource Release
    fd_close_all(current_process);
    

    if (current_process->cwd_node) {
        close_fs(current_process->cwd_node);
        current_process->cwd_node = NULL;
    }

    if (current_process->root_node && current_process->root_node != fs_root) {
        close_fs(current_process->root_node);
        current_process->root_node = NULL;
    }

    if (current_process->pmap && current_process->pmap != pmap_kernel()) {
        pmap_activate(pmap_kernel());
        current_process->pmap = pmap_kernel();
    }
    
    // 3. Reparent Children
    proc_reparent_children(current_process);

    // 4. Phase 2: Timers
    proc_timers_cancel(current_process);
    
    // 5. Phase 2: System V IPC
    proc_sysvipc_exit(current_process);
    
    // 6. Phase 2: POSIX IPC
    proc_posixipc_exit(current_process);
    
    // 8. Phase 3: Thread Termination
    // Current thread becomes the "reaper thread"
    FOREACH_THREAD(thread) {
        if (thread->proc != current_process) continue;
        if (thread == current_thread) continue;
        // If not current thread, interrupt and terminate
        thread->state = THREAD_ZOMBIE;
        // Wake up so it gets preempted/terminated if sleeping
        sleepq_wake_all(thread);
    }
    
    // Wait for all threads to reach zombie state
    // (In our case, setting state=THREAD_ZOMBIE prevents them from being scheduled,
    // and an IPI would force them off remote CPUs. We assume they are effectively dead here.)
    // Free thread stacks and thread structures logic goes here once `kstack_base` is tracked.

    // 9. Controlling Terminal Cleanup
    if (current_process->tty) {
        
        // Check if session leader
        int is_session_leader = 0;
        if (current_process->p_pgrp && 
            current_process->p_pgrp->pg_session &&
            current_process->p_pgrp->pg_session->s_leader == current_process) {
            is_session_leader = 1;
        }
        
        if (is_session_leader) {
            current_process->p_pgrp->pg_session->s_leader = NULL;
            tty_hangup(current_process->tty);
        }
        
        current_process->tty = NULL;
    }
    
    // 5. Calculate final rusage (user + system time)
    rusage_finalize(current_process);
    
    /*
     * Become non-preemptible for the rest of exit.  The moment we publish
     * SZOMB + THREAD_ZOMBIE below, this process is reapable by a waiting
     * parent.  With kernel preemption enabled, a timer tick anywhere between
     * here and the final sched_yield() could switch to the just-woken parent,
     * whose wait4()/SIGCHLD handler would proc_destroy() us — freeing this
     * thread's stack and storage while we are still executing on it.  That
     * use-after-free scribbles the parent's freshly-posted SIGCHLD bit (and
     * other kernel-heap state), so the child-exit notification is silently
     * lost and the parent wedges forever in sigsuspend().  Pinning preemption
     * until our own sched_yield() switches us off-CPU closes the window: the
     * parent cannot run to reap us until we have voluntarily given up the CPU,
     * at which point our stack is no longer live.  We never re-enable — this
     * thread does not return.
     */
    preempt_disable();

    // 6. Record exit status and set state
    current_process->exit_code = code;
    current_process->state = SZOMB;

    /*
     * 7. Mark all of this process's threads as zombies BEFORE notifying
     * the parent / waking waiters.  find_waitable_child gates on
     * wait_threads_all_zombie, which iterates every thread that points
     * at this proc and requires THREAD_ZOMBIE.  If we wake the parent
     * first, the scheduler can run the parent's wait4 loop in the gap
     * between the wake and this transition, see the current thread
     * still non-zombie, conclude "not yet reapable", and re-sleep on
     * the same channel.  Since proc_exit only fires a single wake,
     * that race deadlocks the parent — exactly the "zsh hangs after
     * the first child dies" symptom under PID-1 zsh + `ps`.
     *
     * Order now: state=SZOMB (already set), then mark threads zombie,
     * then signal + wake.  The wake at the end is the only one that
     * matters; by the time the parent observes it, everything the
     * reaper needs is in place.
     */
    FOREACH_THREAD(thread) {
        if (thread->proc != current_process) continue;
        thread->state = THREAD_ZOMBIE;
        sleepq_wake_all(thread);
    }

    // 8. Notify parent (after the SZOMB + thread-ZOMBIE transition so
    //    the wakeup is observed only when the child is fully reapable)
    if (current_process->p_parent) {
        // Check SA_NOCLDWAIT
        int nocldwait = 0;
        if (current_process->p_parent->sig_actions[SIGCHLD-1].sa_flags & SA_NOCLDWAIT) {
             nocldwait = 1;
        }

        if (PROC_DEBUG_ENABLED()) {
            PROC_DEBUG("proc: exit pid=%d parent=%d nocldwait=%d flags=%#x code=%d\n",
                       current_process->pid,
                       current_process->p_parent->pid,
                       nocldwait,
                       current_process->p_parent->sig_actions[SIGCHLD-1].sa_flags,
                       code);
        }

        if (nocldwait) {
             // Notify parent as required
             psignal(current_process->p_parent, SIGCHLD);

             current_process->p_flag |= P_AUTOREAP;
             /* This SZOMB now awaits the idle-loop autoreap sweep; the counter
              * lets that sweep skip its scan entirely until we (and any peers)
              * are reaped.  Every thread of ours is already THREAD_ZOMBIE (set
              * above) so we are immediately reapable. */
             __sync_fetch_and_add(&autoreap_pending, 1);
             proc_remove_child(current_process->p_parent, current_process);
             current_process->p_parent = NULL;
        } else {
             psignal(current_process->p_parent, SIGCHLD);
             // Wake up waiters now that everything they need to observe
             // (SZOMB + all threads ZOMBIE) is in place.
             sched_wakeup(&current_process->p_parent->p_children);
        }
    } else {
        // No parent? (swapper/init special case)
        process_t *init = proc_find(1);
        if (init) {
            sched_wakeup(&init->p_children);
        }
    }

    sched_yield();
    
    // Should not reach here
    while(1);
}

void proc_set_bitness(process_t *p, uint8_t bitness) {
    if (p) {
        p->bitness = bitness;
    }
}

uint8_t proc_get_bitness(process_t *p) {
    return p ? p->bitness : 0;
}
