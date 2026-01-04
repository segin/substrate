#include "pm.h"
#include <sys/acct.h>
#include <sys/file.h>
#include <kern/sched.h> // For MAX_THREADS, thread creation
#include <stddef.h>
#include <string.h>

process_t processes[MAX_PROCS];
process_t *current_process = NULL;
static int next_pid = 1;

extern uint32_t get_time(void);
extern fs_node_t *fs_root;
extern struct personality personality_native;

void pm_init(void) {
    next_pid = 1;
    memset(processes, 0, sizeof(processes));
    
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
    
    return &processes[i];
}

int proc_fork(process_t *parent, void *stack) {
    process_t *child_proc = proc_create(parent->pers);
    if (!child_proc) return -1;
    
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
    #define VIRTUAL_d(x)  ((void*)(uintptr_t)((uint32_t)(x) + 0xC0000000))
    extern void *pmm_alloc_block(void);
    extern struct personality personality_native;
    extern int sched_create_thread(process_t *proc, void (*entry_point)(void*), void *stack, void *arg);

    // 1. Create Process
    process_t *child = proc_create(&personality_native);
    if (!child) return -1;
    
    // 2. Set Name
    strcpy(child->comm, "(kinit)"); 

    // 3. Allocate Stack (4KB)
    void *stack_phys = pmm_alloc_block();
    if (!stack_phys) {
        return -1;
    }
    void *stack_virt = (uint8_t*)VIRTUAL_d(stack_phys) + 4096;
    
    // 4. Create Thread
    int tid = sched_create_thread(child, entry, stack_virt, arg);
    
    return tid;
}
