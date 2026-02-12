#include <pm/pm.h>
#include <sys/acct.h>
#include <sys/file.h>
#include <sys/lock.h>
#include <sys/session.h>
#include <kern/console.h>
#include <kern/sched.h> // For MAX_THREADS, thread creation
#include <stddef.h>
#include <string.h>
#include <arch/i386/pmap.h>
#include <exec/perso/personality.h>

process_t processes[MAX_PROCS];
process_t *current_process = NULL;
process_t *kernel_process = NULL;
static int next_pid = 1;
static spinlock_t pid_lock;
extern thread_t threads[MAX_THREADS];

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

extern uint32_t get_time(void);
extern fs_node_t *fs_root;
extern struct personality personality_native;

/* Forward declarations */
void proc_add_child(process_t *parent, process_t *child);
void proc_remove_child(process_t *parent, process_t *child);

void pm_init(void) {
    next_pid = 1;
    memset(processes, 0, sizeof(processes));
    kernel_process = &processes[0];
    
    /* Initialize the process tree lock */
    mutex_init(&proctree_lock, "proctree");
    spinlock_init(&pid_lock, "pid");
    
    for (int i = 0; i < MAX_PROCS; i++) {
        processes[i].pid = -1;
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
    
    processes[i].pid = next_pid++;
    spinlock_release(&pid_lock);
    processes[i].ppid = current_process ? current_process->pid : 0;
    processes[i].perso_id = perso_id;
    processes[i].root_node = current_process ? current_process->root_node : fs_root;
    processes[i].next_fd = 0; // Reset FD hint
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
    
    return &processes[i];
}

int proc_fork(process_t *parent, void *stack) {
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
    
    return t ? t->tid : -1;
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
            p->fds[i] = NULL;
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

void proc_exit(int code) {
    if (current_process->pid == 1) {
        kprint("Warning: Init process exited. System Halted (idle).\n");
        while(1) { __asm__ volatile("hlt"); }
    }
    
    // 1. Set State
    current_process->state = SDYING;
    current_process->exit_code = code;
    
    // 1b. Thread Cleanup (Robust Futexes & Pending Signals)
    extern void futex_thread_exit(thread_t *t);
    for (int i = 0; i < MAX_THREADS; i++) {
        if (threads[i].tid != -1 && threads[i].proc == current_process) {
            /* Cleanup robust futexes for this thread */
            futex_thread_exit(&threads[i]);
            
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

    if (current_process->vm_map) {
        extern void vm_map_destroy(struct vm_map *map);
        // vm_map_destroy(current_process->vm_map);
        current_process->vm_map = NULL;
    }
    
    if (current_process->pmap && current_process->pmap != pmap_kernel()) {
        pmap_release(current_process->pmap);
        current_process->pmap = pmap_kernel(); // Switch to kernel map safe fallback?
        // Actually cr3 switch happens on context switch.
        // We shouldn't free pmap if we are running on it?
        // Traditionally, we switch to swapper's pmap or kernel pmap before freeing.
        // But for now, just decrement ref.
    }
    
    // 3. Reparent Children
    proc_reparent_children(current_process);

    // 4. Controlling Terminal Cleanup
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
            tty_hangup(current_process->tty);
        }
        
        current_process->tty = NULL;
    }
    
    // 5. Notify Parent
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
             
             // Reparent to Init for automatic reaping
             process_t *init = &processes[1]; // PID 1
             if (init != current_process) { // Don't reparent init to itself
                 proc_remove_child(current_process->p_parent, current_process);
                 proc_add_child(init, current_process);
             }
        } else {
             extern void psignal(process_t *p, int sig);
             psignal(current_process->p_parent, SIGCHLD);
             // Wakeup parent
             sched_wakeup(&current_process->p_parent->p_children);
        }
    } else {
        // No parent? (swapper/init special case)
        // If PID > 1, should have parent.
        // Fallback: Wakeup swapper?
        sched_wakeup(&processes[1].p_children); // Wake init just in case
    }
    
    // 5. Calculate final rusage (user + system time)
    extern void rusage_finalize(process_t *p);
    rusage_finalize(current_process);
    
    // 6. Record exit status and set state
    current_process->exit_code = code;
    current_process->state = SZOMB;
    
    // 7. Prevent further scheduling of ALL process threads
    for (int i = 0; i < MAX_THREADS; i++) {
        if (threads[i].tid != -1 && threads[i].proc == current_process) {
            threads[i].state = THREAD_ZOMBIE;
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
