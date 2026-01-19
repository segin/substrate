#include "pm.h"
#include <sys/acct.h>
#include <sys/file.h>
#include <sys/lock.h>
#include <kern/console.h>
#include <kern/sched.h> // For MAX_THREADS, thread creation
#include <stddef.h>
#include <string.h>
#include "../arch/i386/pmap.h"

process_t processes[MAX_PROCS];
process_t *current_process = NULL;
static int next_pid = 1;

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

void pm_init(void) {
    next_pid = 1;
    memset(processes, 0, sizeof(processes));
    
    /* Initialize the process tree lock */
    mutex_init(&proctree_lock, "proctree");
    
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

process_t *proc_create(struct personality *pers) {
    int i;
    for (i = 0; i < MAX_PROCS; i++) {
        if (processes[i].pid == -1) break;
    }
    if (i == MAX_PROCS) return NULL;
    
    processes[i].pid = next_pid++;
    processes[i].ppid = current_process ? current_process->pid : 0;
    processes[i].pers = pers;
    processes[i].root_node = current_process ? current_process->root_node : fs_root;
    for(int j=0; j<MAX_FD; j++) processes[i].fds[j] = 0;
    
    // Acct init
    strcpy(processes[i].comm, "forked");
    processes[i].start_time = get_time();
    processes[i].uid = current_process ? current_process->uid : 0;
    processes[i].gid = current_process ? current_process->gid : 0;
    
    // Initialize rusage structures
    extern void rusage_init(process_t *p);
    rusage_init(&processes[i]);
    
    return &processes[i];
}

int proc_fork(process_t *parent, void *stack) {
    process_t *child_proc = proc_create(parent->pers);
    if (!child_proc) return -1;
    
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
    for(int j=0; j<MAX_FD; j++) {
        if (parent->fds[j]) {
            child_proc->fds[j] = parent->fds[j];
            child_proc->fds[j]->ref_count++;
        }
    }
    
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
    return sched_fork_thread(child_proc, stack);
}

// Shim for syscalls
int sched_fork_process(process_t *parent, void *stack) {
    return proc_fork(parent, stack);
}

int sched_spawn_kernel_process(void (*entry)(void*), void *arg) {
    extern void *pmm_alloc_block(void);
    extern struct personality personality_native;
    extern int sched_create_thread(process_t *proc, void (*entry_point)(void*), void *stack, void *arg);

    // 1. Create Process
    process_t *child = proc_create(&personality_native);
    if (!child) return -1;
    
    // 2. Set Name
    strcpy(child->comm, "(kinit)"); 

    // 3. Allocate Stack (4KB) - pmm_alloc_block returns virtual address
    void *stack = pmm_alloc_block();
    if (!stack) {
        return -1;
    }
    void *stack_top = (uint8_t*)stack + 4096;
    
    // 4. Create Thread
    int tid = sched_create_thread(child, entry, stack_top, arg);
    
    return tid;
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
    extern void acct_process(int code);
    acct_process(code);
    
    // 2. Resource Release
    fd_close_all(current_process);
    
    if (current_process->vm_map) {
        // VM Map release
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
    
    // 4. Notify Parent
    if (current_process->p_parent) {
        // SIGCHLD
        extern void psignal(process_t *p, int sig);
        psignal(current_process->p_parent, SIGCHLD);
        
        // Wakeup parent (waiting on p_children address usually)
        sched_wakeup(&current_process->p_parent->p_children);
    } else {
        // No parent? (swapper/init special case)
        // If PID > 1, should have parent.
        // Fallback: Wakeup swapper?
        sched_wakeup(&processes[1].p_children); // Wake init just in case
    }
    
    // 5. Calculate final rusage (user + system time)
    extern void rusage_finalize(process_t *p);
    rusage_finalize(current_process);
    
    // 6. Final State
    current_process->state = SZOMB;
    
    // 6. Reschedule (never returns)
    // Mark current thread as zombie too?
    current_thread->state = THREAD_ZOMBIE;
    
    sched_yield();
    
    // Should not reach here
    while(1);
}
