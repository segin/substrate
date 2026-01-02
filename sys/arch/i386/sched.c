#include "../../kern/sched.h"
#include "../../pm/pm.h"
#include <sys/acct.h>
#include <stddef.h>
#include <stdint.h>

// Arch-Specific Externs
extern void switch_to(thread_t *prev, thread_t *next);
extern void set_kernel_stack(uint32_t stack);
extern thread_t *current_thread; // Now defined in generic sched.c
extern process_t processes[]; // from pm
extern fs_node_t *fs_root;
extern struct personality personality_native;
extern uint32_t get_time(void);

// Generic Allocation Helper
extern thread_t *sched_alloc_thread(process_t *proc);
extern void sched_init_generic(void);

// Exposed to Generic Scheduler
void arch_switch_to(thread_t *prev, thread_t *next) {
    switch_to(prev, next);
}

void arch_set_kernel_stack(uintptr_t stack) {
    set_kernel_stack((uint32_t)stack);
}

// Thread exit wrapper - called when a thread's entry_point returns
static void thread_exit_wrapper(void) {
    if (current_thread) {
        current_thread->state = THREAD_ZOMBIE;
    }
    sched_yield();
    // Should never reach here, but halt if we do
    while(1) { __asm__ volatile("hlt"); }
}

void sched_init(void) {
    sched_init_generic();

    // Arch-Specific: Setup Initial Kernel Thread (TID 1)
    // sched_init_generic zeroes things. We rely on it.
    // Manually setup threads[0] equivalent logic, but via alloc?
    // alloc requires a process.
    
    // Setup Kernel Process (PID 1)
    processes[0].pid = 1;
    processes[0].ppid = 0;
    processes[0].pers = &personality_native;
    processes[0].root_node = fs_root;
    // ... init FDs
    
    // Copy permissions/acct
    // ... (simplified)
    
    // Alloc Kernel Thread
    // We can't use alloc because it might pick index 0 which is fine, 
    // but 'current_thread' is NULL so alloc might crash on priority inheritance?
    // Modified alloc to handle NULL current_thread.
    
    thread_t *t = sched_alloc_thread(&processes[0]);
    t->state = THREAD_RUNNING;
    t->priority = 20;
    t->base_priority = 20;
    
    current_thread = t;
    current_process = &processes[0];
}

int sched_fork_thread(process_t *proc, void *stack) {
    thread_t *t = sched_alloc_thread(proc);
    if (!t) return -1;
    
    // Setup Child Stack (simulated fork return)
    t->kstack_ptr = (uintptr_t)stack; // Placeholder
    t->instr_ptr = 0; 

    return proc->pid;
}

int sched_create_thread(process_t *proc, void (*entry_point)(void*), void *stack, void *arg) {
    thread_t *t = sched_alloc_thread(proc);
    if (!t) return -1;
    
    // Simulate stack frame for "entry_point(arg)"
    uint32_t *stk = (uint32_t*)stack;
    
    // Correct stack for switch_to (same as before):
    // [Arg]
    // [0] (Ret addr for entry_point)
    // [exit_wrapper]
    // [entry_point] (saved EIP for switch_to)
    // [ebp, edi, esi, ebx]
    
    stk--; *stk = (uint32_t)(uintptr_t)arg;
    stk--; *stk = (uint32_t)(uintptr_t)thread_exit_wrapper; 
    stk--; *stk = (uint32_t)(uintptr_t)entry_point; 
    stk--; *stk = 0; // ebp
    stk--; *stk = 0; // edi
    stk--; *stk = 0; // esi
    stk--; *stk = 0; // ebx
    
    t->kstack_ptr = (uintptr_t)stk; 
    t->instr_ptr = (uintptr_t)entry_point;

    return t->tid;
}





