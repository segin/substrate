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
    processes[0].pid = 0;
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

int sched_fork_thread(process_t *proc, void *parent_regs) {
    // Cast parent_regs to registers_t pointer
    typedef struct {
        uint32_t ds;
        uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;
        uint32_t int_no, err_code;
        uint32_t eip, cs, eflags, useresp, ss;
    } fork_regs_t;
    
    fork_regs_t *regs = (fork_regs_t *)parent_regs;
    
    thread_t *t = sched_alloc_thread(proc);
    if (!t) return -1;
    
    // Allocate kernel stack for child (8KB)
    extern void *pmm_alloc_block(void);
    // Allocate 2 pages = 8KB kernel stack
    void *kstack_base = pmm_alloc_block();
    void *kstack_base2 = pmm_alloc_block();
    if (!kstack_base || !kstack_base2) return -1;
    
    // Stack is at top of these pages (converted to virtual)
    // In higher-half kernel, physical needs to be converted
    #define P2V(x) ((void*)((uint32_t)(x) + 0xC0000000))
    uint32_t *kstack = (uint32_t *)((uint32_t)P2V(kstack_base2) + 0x1000);
    t->kstack_top = (uintptr_t)kstack;

    // Build IRET frame on child's kernel stack
    // push in reverse order (stack grows down)
    kstack--; *kstack = regs->ss;         // SS
    kstack--; *kstack = regs->useresp;    // User ESP
    kstack--; *kstack = regs->eflags;     // EFLAGS
    kstack--; *kstack = regs->cs;         // CS
    kstack--; *kstack = regs->eip;        // EIP - resume at same instruction
    kstack--; *kstack = regs->err_code;   // Error code
    kstack--; *kstack = regs->int_no;     // Int number
    
    // Push pusha registers (child gets EAX = 0 = fork return value)
    kstack--; *kstack = 0;                // EAX = 0 (child's return value!)
    kstack--; *kstack = regs->ecx;
    kstack--; *kstack = regs->edx;
    kstack--; *kstack = regs->ebx;
    kstack--; *kstack = regs->esp;        // Original ESP (ignored by popa)
    kstack--; *kstack = regs->ebp;
    kstack--; *kstack = regs->esi;
    kstack--; *kstack = regs->edi;
    
    // Push DS
    kstack--; *kstack = regs->ds;
    
    // Now push the callee-saved registers for switch_to
    // switch_to expects: [ebx, esi, edi, ebp] at stack top
    kstack--; *kstack = 0;  // EBP
    kstack--; *kstack = 0;  // EDI  
    kstack--; *kstack = 0;  // ESI
    kstack--; *kstack = 0;  // EBX
    
    // Return address for switch_to - point to isr_exit to do the IRET
    extern void fork_child_return(void);
    kstack--; *kstack = (uint32_t)fork_child_return;
    
    t->kstack_ptr = (uintptr_t)kstack;
    t->instr_ptr = regs->eip;
    
    return proc->pid;
}

int sched_create_thread(process_t *proc, void (*entry_point)(void*), void *stack, void *arg) {
    thread_t *t = sched_alloc_thread(proc);
    if (!t) return -1;
    
    // Simulate stack frame for "entry_point(arg)"
    uint32_t *stk = (uint32_t*)stack;
    t->kstack_top = (uintptr_t)stk;
    
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





