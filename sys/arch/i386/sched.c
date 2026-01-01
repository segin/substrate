#include "../../kern/sched.h"
#include "../../sys/acct.h"
#include <stddef.h>

// Simple memcpy/strcpy if not available in kernel lib yet, or assume built-in
void k_strcpy(char *dst, const char *src) {
    while((*dst++ = *src++));
}

#define MAX_THREADS 64
#define MAX_PROCS   16

static thread_t threads[MAX_THREADS];
static process_t processes[MAX_PROCS];

thread_t *current_thread = 0;
process_t *current_process = 0;

static int next_tid = 1;
static int next_pid = 1;

extern uint32_t get_time(void);
extern void switch_to(thread_t *prev, thread_t *next);
extern void set_kernel_stack(uint32_t stack);

void sched_init(void) {
    // Zero out arrays
    for (int i = 0; i < MAX_THREADS; i++) {
        threads[i].tid = -1;
        threads[i].state = THREAD_ZOMBIE;
    }
    for (int i = 0; i < MAX_PROCS; i++) {
        processes[i].pid = -1;
    }

    // Create Initial Kernel Process
    processes[0].pid = next_pid++;
    processes[0].pers = &personality_native;
    for(int j=0; j<MAX_FD; j++) processes[0].fds[j] = 0;
    
    // Acct init
    k_strcpy(processes[0].comm, "kernel");
    processes[0].start_time = get_time();
    processes[0].uid = 0;
    processes[0].gid = 0;

    // Create Initial Kernel Thread
    threads[0].tid = next_tid++;
    threads[0].proc = &processes[0];
    threads[0].state = THREAD_RUNNING;
    
    current_process = &processes[0];
    current_thread = &threads[0];
}

process_t *sched_create_process(struct personality *pers) {
    int i;
    for (i = 0; i < MAX_PROCS; i++) {
        if (processes[i].pid == -1) break;
    }
    if (i == MAX_PROCS) return NULL;
    
    processes[i].pid = next_pid++;
    processes[i].pers = pers;
    for(int j=0; j<MAX_FD; j++) processes[i].fds[j] = 0;
    
    // Acct init
    k_strcpy(processes[i].comm, "forked"); // Should be inherited or set by exec
    processes[i].start_time = get_time();
    processes[i].uid = current_process ? current_process->uid : 0;
    processes[i].gid = current_process ? current_process->gid : 0;
    
    return &processes[i];
}

// Fork: Create a new process that is a copy of parent
int sched_fork_process(process_t *parent, void *stack) {
    process_t *child_proc = sched_create_process(parent->pers);
    if (!child_proc) return -1;
    
    // Copy parent resources (FDs)
    for(int j=0; j<MAX_FD; j++) {
        if (parent->fds[j]) {
            child_proc->fds[j] = parent->fds[j];
            child_proc->fds[j]->ref_count++;
        }
    }
    
    int i;
    for (i = 0; i < MAX_THREADS; i++) {
        if (threads[i].tid == -1) break;
    }
    if (i == MAX_THREADS) return -1;

    threads[i].tid = next_tid++;
    threads[i].proc = child_proc;
    threads[i].state = THREAD_READY;
    
    // Setup Child Stack
    // For fork/clone(PROCESS), execution usually continues at the instruction after the syscall in the child.
    // This requires copying the *kernel stack* of the parent so that when the scheduler switches to child,
    // it 'returns' from the syscall just like the parent.
    // Since we don't have full kernel stack management here, we'll cheat:
    // We assume the caller provided a 'stack' (user stack).
    // In a real OS, fork() duplicates the address space.
    // Here we share address space (threads), so 'fork' is just a thread in a new process container.
    
    // We'll treat it like a thread start for now, assuming the syscall wrapper handles the "am I child?" logic via return value.
    // BUT, we need an entry point. 
    // Linux clone() takes a child_stack.
    
    // If we assume this is called via sys_clone, we might set EIP to the return address of the syscall?
    // Too complex for this stub. We'll set it to 0 for now or assume the same entry point logic.
    
    threads[i].kstack_ptr = (uintptr_t)stack; // Incorrect usage of kstack_ptr, but acts as placeholder
    threads[i].instr_ptr = 0; 

    return child_proc->pid;
}

int sched_create_thread(process_t *proc, void (*entry_point)(void*), void *stack, void *arg) {
    int i;
    for (i = 0; i < MAX_THREADS; i++) {
        if (threads[i].tid == -1) break;
    }
    if (i == MAX_THREADS) return -1;

    threads[i].tid = next_tid++;
    threads[i].proc = proc;
    threads[i].state = THREAD_READY;
    
    // Simulate stack frame for "entry_point(arg)"
    uint32_t *stk = (uint32_t*)stack;
    stk--; *stk = (uint32_t)arg;  // Arg
    stk--; *stk = 0;              // Fake Return Addr (or a thread_exit wrapper)
    
    // For switch_to: it will pop ebp, edi, esi, ebx
    stk--; *stk = (uint32_t)entry_point; // This will be popped into EBP or something? 
                                         // Wait, switch_to returns to whatever is on stack.
                                         // If we want it to "return" to entry_point, 
                                         // we should put entry_point where the 'ret' expects it.
    
    // Correct stack for switch_to:
    // [Arg]
    // [0] (Ret addr for entry_point)
    // [entry_point] (This is where switch_to's 'ret' will go)
    // [ebp]
    // [edi]
    // [esi]
    // [ebx]
    
    stk = (uint32_t*)stack;
    stk--; *stk = (uint32_t)arg;
    stk--; *stk = 0; // Return address from entry_point
    stk--; *stk = (uint32_t)entry_point; // Return address for switch_to
    stk--; *stk = 0; // ebp
    stk--; *stk = 0; // edi
    stk--; *stk = 0; // esi
    stk--; *stk = 0; // ebx
    
    threads[i].kstack_ptr = (uintptr_t)stk; // ESP
    threads[i].instr_ptr = (uintptr_t)entry_point; // EIP (informational)

    return threads[i].tid;
}

void sched_yield(void) {
    if (!current_thread) return;

    // Simple Round Robin
    thread_t *next = current_thread + 1;
    if (next >= &threads[MAX_THREADS]) next = &threads[0];

    int count = 0;
    while (next->state != THREAD_READY && next != current_thread && count < MAX_THREADS) {
        next++;
        if (next >= &threads[MAX_THREADS]) next = &threads[0];
        count++;
    }

    if (next == current_thread) return;

    if (next->state == THREAD_READY) {
        thread_t *prev = current_thread;
        
        prev->state = THREAD_READY;
        next->state = THREAD_RUNNING;
        
        current_thread = next;
        current_process = current_thread->proc;
        
        // Update TSS kernel stack for interrupts (if we had a separate kernel stack per thread)
        // Here we assume kstack_ptr is the top of the kernel stack for this thread.
        // Actually we should store the base somewhere.
        // For now, let's just use the current kstack_ptr as a hint.
        set_kernel_stack(current_thread->kstack_ptr); 

        switch_to(prev, next);
    }
}

int sched_get_current_tid(void) {
    if (current_thread) return current_thread->tid;
    return -1;
}