#include <pm/pm.h>
#include <sys/acct.h>
#include <sys/file.h>
#include <sys/lock.h>
#include <sys/session.h>
#include <kern/console.h>
#include <kern/sched.h> // For MAX_THREADS, thread creation
#include <kern/sleepq.h>
#include <stddef.h>
#include <string.h>
#include <arch/i386/pmap.h>
#include <arch/i386/intr.h>
#include <vm/vm_map.h>
#include <exec/perso/personality.h>
#include <kern/time.h>

process_t processes[MAX_PROCS];
process_t *current_process = NULL;
process_t *kernel_process = NULL;
static int next_pid = 1;
static spinlock_t pid_lock;


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

extern fs_node_t *fs_root;
extern struct personality personality_native;

static void proc_idle_wait(void) {
#ifdef HOST_TEST
    extern void host_wait_for_interrupt(void);
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

void pm_init(void) {
    next_pid = 1;
    memset(processes, 0, sizeof(processes));
    kernel_process = &processes[0];
    
    /* Initialize the process tree lock */
    mutex_init(&proctree_lock, "proctree");
    spinlock_init(&pid_lock, "pid");
    
    for (int i = 0; i < MAX_PROCS; i++) {
        processes[i].pid = -1;
        proc_timers_init(&processes[i]);
    }

    // Create Initial Kernel Process (PID 1? Or 0?)
    // sched.c used to do this.
    // We should probably let sched.c init the *first* thread/proc manually or calling this?
    // Sched init usually sets up idle/kernel task.
    
    // Let's allow pm_init to setup the array, but sched_init might populate the first one?
    // Or we provide a function to allocate the first one.
}

/*
 * proc_find - Find a process by PID
 *
 * Searches the process table for a process with the given PID.
 * Returns NULL if no active process with that PID exists.
 */
process_t *proc_find(int pid) {
    for (int i = 0; i < MAX_PROCS; i++) {
        if (processes[i].pid == pid) {
            return &processes[i];
        }
    }
    return NULL;
}

int proc_get_last_pid(void) {
    return next_pid - 1;
}

process_t *proc_create(int perso_id) {
    spinlock_acquire(&pid_lock);
    int i;
    for (i = 0; i < MAX_PROCS; i++) {
        if (processes[i].pid == -1) break;
    }
    if (i == MAX_PROCS) {
        spinlock_release(&pid_lock);
        return NULL;
    }
    
    int pid = next_pid++;
    spinlock_release(&pid_lock);
    memset(&processes[i], 0, sizeof(processes[i]));
    processes[i].pid = pid;
    processes[i].ppid = current_process ? current_process->pid : 0;
    processes[i].perso_id = perso_id;
    processes[i].root_node = current_process ? current_process->root_node : fs_root;
    processes[i].next_fd = 0; // Reset FD hint
    processes[i].fd_bitmap = 0;
    for(int j=0; j<MAX_FD; j++) processes[i].fds[j] = 0;
    
    // Acct init
    strncpy(processes[i].comm, "forked", AC_COMM_LEN);
    processes[i].comm[AC_COMM_LEN - 1] = '\0';
    processes[i].start_time = get_time();
    processes[i].uid = current_process ? current_process->uid : 0;
    processes[i].gid = current_process ? current_process->gid : 0;
    processes[i].euid = current_process ? current_process->euid : 0;
    processes[i].egid = current_process ? current_process->egid : 0;
    processes[i].suid = current_process ? current_process->suid : 0;
    processes[i].sgid = current_process ? current_process->sgid : 0;
    
    // Initialize rusage structures
    extern void rusage_init(process_t *p);
    rusage_init(&processes[i]);
    proc_timers_init(&processes[i]);
    
    return &processes[i];
}

static int proc_fork_common(process_t *parent, void *stack, int is_vfork) {
    process_t *child_proc = proc_create(parent->perso_id);
    if (!child_proc) return -1;
    
    // Inherit process name
    strncpy(child_proc->comm, parent->comm, AC_COMM_LEN);
    child_proc->comm[AC_COMM_LEN - 1] = '\0';
    
    // Clone parent's address space with COW
    if (parent->pmap) {
        child_proc->pmap = pmap_fork(parent->pmap);
        if (!child_proc->pmap) {
            // Failed to clone pmap
            child_proc->pid = -1; // Mark as free
            return -1;
        }
    } else {
        child_proc->pmap = NULL; // Kernel process (shouldn't fork)
    }
    
    // Copy cwd_node
    child_proc->cwd_node = parent->cwd_node;
    
    // Copy parent resources (FDs)
    child_proc->tty = parent->tty;
    child_proc->bitness = parent->bitness;
    child_proc->vfork_waiter = is_vfork ? current_thread : NULL;

    child_proc->fd_bitmap = parent->fd_bitmap;
    for(int j=0; j<MAX_FD; j++) {
        if (parent->fds[j]) {
            child_proc->fds[j] = parent->fds[j];
            child_proc->fds[j]->f_count++;
        }
    }
    
    // Copy Process Group and Session (inherit from parent)
    mutex_lock(&proctree_lock);
    child_proc->p_pgrp = parent->p_pgrp;
    child_proc->p_pgrp_link = parent->p_pgrp_link; // Wait, we need to link into the group list properly!
    
    /* 
     * Correctly adding to process group:
     * We can't just copy the pointer 'p_pgrp_link' - that would corrupt the linked list.
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
    extern void *memcpy(void*, const void*, size_t);
    memcpy(child_proc->sig_actions, parent->sig_actions, sizeof(parent->sig_actions));
    child_proc->sig_catch = parent->sig_catch;
    child_proc->sig_ignore = parent->sig_ignore;
    
    // Copy limits, etc. if implemented
    
    // Create Thread for child
    // Implementation of this part relies on sched logic (threading).
    // So PM depends on SCHED for thread creation.
    
    // sched_fork_thread logic?
    // In sched.c it was inlined.
    // We need sched_create_thread implementation to support "fork" style (copy regs).
    // Or we just call sched_create_thread with a special entry point?
    
    // The previous implementation manually manipulated thread array to copy state.
    // This implies sched.c should handle the thread part of fork.
    // Maybe proc_fork should call `sched_fork_thread(child_proc, stack)`?
    
    // We'll declare `sched_fork_thread` in sched.h and assume it exists (we'll move it there).
    extern int sched_fork_thread(process_t *proc, void *stack);
    
    /* 
     * Link child into parent's list.
     */
    proc_add_child(parent, child_proc);
    
    return sched_fork_thread(child_proc, stack);
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
    mutex_lock(&proctree_lock);
    child->p_parent = parent;
    child->p_sibling = parent->p_children;
    parent->p_children = child;
    mutex_unlock(&proctree_lock);
}

void proc_remove_child(process_t *parent, process_t *child) {
    if (!parent || !child) return;
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
    if (!child || child->vfork_waiter != current_thread) {
        return 0;
    }

    sched_sleep(child);
    return 0;
}

void proc_vfork_done(process_t *child) {
    if (!child || !child->vfork_waiter) {
        return;
    }

    child->vfork_waiter = NULL;
    sched_wakeup(child);
}

int sched_spawn_kernel_process(void (*entry)(void*), void *arg) {
    extern void *pmm_alloc_block(void);
    extern thread_t *sched_create_thread(process_t *proc, void (*entry_point)(void*), void *stack, void *arg);

    // 1. Create Process
    process_t *child = proc_create(PERS_NATIVE);
    if (!child) return -1;
    
    // 2. Set Name
    strncpy(child->comm, "(kinit)", AC_COMM_LEN);
    child->comm[AC_COMM_LEN - 1] = '\0';

    // 3. Allocate Stack (4KB) - pmm_alloc_block returns virtual address
    void *stack = pmm_alloc_block();
    if (!stack) {
        return -1;
    }
    void *stack_top = (uint8_t*)stack + 4096;
    
    // 4. Create Thread
    thread_t *t = sched_create_thread(child, entry, stack_top, arg);
    if (!t) {
        extern void pmm_free_block(void *p);
        pmm_free_block(stack);
        return -1;
    }

    t->kstack_base = (uintptr_t)stack;
    t->kstack_units = 1;
    t->kstack_type = THREAD_KSTACK_PMM_BLOCK;
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
            extern void file_close_ptr(file_t *f); // Need to add this to syscall.c or file.c
            file_close_ptr(p->fds[i]);
            proc_clear_fd(p, i);
        }
    }
}

int proc_alloc_fd(process_t *p) {
    if (!p) return -1;

    uint32_t valid_mask = (MAX_FD == 32) ? 0xFFFFFFFF : ((1U << MAX_FD) - 1);
    uint32_t free_bits = (~p->fd_bitmap) & valid_mask;
    if (free_bits == 0) return -1;

    int start = p->next_fd;
    if (start < 0 || start >= MAX_FD) start = 0;

    uint32_t mask = free_bits & ~((1U << start) - 1);
    int fd = -1;

    if (mask != 0) {
        fd = __builtin_ctz(mask);
    } else {
        // Wrap around
        fd = __builtin_ctz(free_bits);
    }

    if (fd != -1) {
        p->next_fd = (fd + 1) % MAX_FD;
        p->fd_bitmap |= (1U << fd); // Mark as reserved
    }

    return fd;
}

void proc_set_fd(process_t *p, int fd, file_t *f) {
    if (!p || fd < 0 || fd >= MAX_FD) return;
    p->fds[fd] = f;
    if (f) {
        p->fd_bitmap |= (1U << fd);
    } else {
        p->fd_bitmap &= ~(1U << fd);
    }
}

void proc_clear_fd(process_t *p, int fd) {
    proc_set_fd(p, fd, NULL);
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
    
    init = &processes[1]; // Assume PID 1 is index 1 or we search
    if (init->state == SDYING || init->state == SZOMB) {
        // init is dying, reparent to swapper?
        init = &processes[0];
    }
    
    child = p->p_children;
    while (child) {
        next = child->p_sibling;
        
        // Remove from p's list (conceptually done by iterating)
        child->p_parent = init;
        
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
    for (int i = 0; i < MAX_THREADS; i++) {
        if (threads[i].tid == -1 || threads[i].proc != proc || &threads[i] == skip_thread) {
            continue;
        }
        if (threads[i].state != THREAD_ZOMBIE) {
            return 0;
        }
    }
    return 1;
}

static void proc_release_zombie_resources(process_t *proc) {
    if (!proc) {
        return;
    }

    if (proc->vm_map) {
        vm_map_destroy(proc->vm_map);
        proc->vm_map = NULL;
    } else if (proc->pmap && proc->pmap != pmap_kernel()) {
        pmap_release(proc->pmap);
    }

    if (proc->p_pgrp) {
        extern void pgrp_remove_proc(struct process *proc);
        pgrp_remove_proc(proc);
    }

    proc->pmap = pmap_kernel();
}

void proc_reap_autoreap_zombies(void) {
    for (int i = 0; i < MAX_PROCS; i++) {
        process_t *proc = &processes[i];

        if (proc->pid <= 0 || proc->state != SZOMB || !(proc->p_flag & P_AUTOREAP)) {
            continue;
        }
        if (current_thread && current_thread->proc == proc) {
            continue;
        }
        if (!proc_threads_all_zombie(proc, NULL)) {
            continue;
        }

        proc_release_zombie_resources(proc);
        sched_reap_process_threads(proc);

        proc->pid = -1;
        proc->ppid = 0;
        proc->state = 0;
        proc->p_flag = 0;
        proc->p_parent = NULL;
        proc->p_children = NULL;
        proc->p_sibling = NULL;
    }
}

void proc_exit(int code) {
    if (current_process->pid == 1) {
        kprint("Warning: Init process exited. System Halted (idle).\n");
        for (;;) {
            proc_idle_wait();
        }
    }

    proc_vfork_done(current_process);
    
    // 1. Set State
    current_process->state = SDYING;
    current_process->exit_code = code;
    
    // 1b. Thread Cleanup (Robust Futexes & Pending Signals)
    extern void futex_thread_exit(thread_t *t);
    extern int mutex_release_owned_by_thread(thread_t *owner);
    extern void kprint(const char *msg);
    for (int i = 0; i < MAX_THREADS; i++) {
        if (threads[i].tid != -1 && threads[i].proc == current_process) {
            /* Cleanup robust futexes for this thread */
            futex_thread_exit(&threads[i]);

            if (mutex_release_owned_by_thread(&threads[i]) > 0) {
                kprint("proc_exit: force-releasing mutexes held by exiting thread.\n");
            }

            /* Remove sleepers from sleep queues before they become zombies. */
            sleepq_remove_thread(&threads[i]);
            
            /* Clear pending signals - process is dying */
            threads[i].sig_pending = 0;
            
            /* Mark as zombie to stop execution (if not current thread) */
            if (&threads[i] != current_thread) {
                threads[i].state = THREAD_ZOMBIE;
            }
        }
    }

    extern void acct_process(int code);
    acct_process(code);
    
    // 2. Resource Release
    fd_close_all(current_process);
    
    extern void close_fs(fs_node_t *node);
    extern fs_node_t *fs_root;

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
    for (int i = 0; i < MAX_THREADS; i++) {
        if (threads[i].tid != -1 && threads[i].proc == current_process) {
            if (&threads[i] != current_thread) {
                // If not current thread, interrupt and terminate
                threads[i].state = THREAD_ZOMBIE;
                // Wake up so it gets preempted/terminated if sleeping
                sleepq_wake_all(&threads[i]);
            }
        }
    }
    
    // Wait for all threads to reach zombie state
    // (In our case, setting state=THREAD_ZOMBIE prevents them from being scheduled,
    // and an IPI would force them off remote CPUs. We assume they are effectively dead here.)
    // Free thread stacks and thread structures logic goes here once `kstack_base` is tracked.

    // 9. Controlling Terminal Cleanup
    if (current_process->tty) {
        extern void tty_hangup(struct tty *tty);
        
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
    extern void rusage_finalize(process_t *p);
    rusage_finalize(current_process);
    
    // 6. Record exit status and set state
    current_process->exit_code = code;
    current_process->state = SZOMB;

    // 7. Notify Parent (after transition to SZOMB to avoid missed wait wakeups)
    if (current_process->p_parent) {
        // Check SA_NOCLDWAIT
        int nocldwait = 0;
        if (current_process->p_parent->sig_actions[SIGCHLD-1].sa_flags & SA_NOCLDWAIT) {
             nocldwait = 1;
        }

        if (nocldwait) {
             // Notify parent as required
             extern void psignal(process_t *p, int sig);
             psignal(current_process->p_parent, SIGCHLD);
             
             current_process->p_flag |= P_AUTOREAP;
             proc_remove_child(current_process->p_parent, current_process);
             current_process->p_parent = NULL;
        } else {
             extern void psignal(process_t *p, int sig);
             psignal(current_process->p_parent, SIGCHLD);
             // Wake up waiters after child is fully waitable.
             sched_wakeup(&current_process->p_parent->p_children);
        }
    } else {
        // No parent? (swapper/init special case)
        sched_wakeup(&processes[1].p_children); // Wake init just in case
    }
    
    // 8. Prevent further scheduling of ALL process threads and wake joiners
    for (int i = 0; i < MAX_THREADS; i++) {
        if (threads[i].tid != -1 && threads[i].proc == current_process) {
            threads[i].state = THREAD_ZOMBIE;
            sleepq_wake_all(&threads[i]);
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
