#include "../../kern/sched.h"
#include "../../sys/acct.h"
#include <stddef.h>
#include <stdint.h>

// Simple memcpy/strcpy if not available in kernel lib yet, or assume built-in
void k_strcpy(char *dst, const char *src) {
    while((*dst++ = *src++));
}

#define MAX_THREADS 64
#define MAX_PROCS   16

thread_t threads[MAX_THREADS];
process_t processes[MAX_PROCS];

thread_t *current_thread = 0;
process_t *current_process = 0;

static int next_tid = 1;
static int next_pid = 1;

extern uint32_t get_time(void);
extern void switch_to(thread_t *prev, thread_t *next);
extern void set_kernel_stack(uint32_t stack);

void sched_init(void) {
    next_tid = 1;
    next_pid = 1;
    
    extern void *memset(void *s, int c, size_t n);
    memset(threads, 0, sizeof(threads));
    memset(processes, 0, sizeof(processes));

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
    processes[0].ppid = 0; // Kernel has no parent
    processes[0].pers = &personality_native;
    processes[0].root_node = fs_root;
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
    threads[0].priority = 20;
    threads[0].base_priority = 20;
    threads[0].sched_class = SCHED_TIMESHARE;
    
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
    processes[i].ppid = current_process ? current_process->pid : 0;
    processes[i].pers = pers;
    processes[i].root_node = current_process ? current_process->root_node : fs_root;
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
    threads[i].priority = current_thread->priority;
    threads[i].base_priority = current_thread->base_priority;
    threads[i].sched_class = current_thread->sched_class;
    
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
    threads[i].priority = 20;
    threads[i].base_priority = 20;
    threads[i].sched_class = SCHED_TIMESHARE;
    
    // Simulate stack frame for "entry_point(arg)"
    uint32_t *stk = (uint32_t*)stack;
    stk--; *stk = (uint32_t)(uintptr_t)arg;  // Arg
    stk--; *stk = 0;              // Fake Return Addr (or a thread_exit wrapper)
    
    // For switch_to: it will pop ebp, edi, esi, ebx
    stk--; *stk = (uint32_t)(uintptr_t)entry_point; // This will be popped into EBP or something? 
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
    stk--; *stk = (uint32_t)(uintptr_t)arg;
    stk--; *stk = 0; // Return address from entry_point
    stk--; *stk = (uint32_t)(uintptr_t)entry_point; // Return address for switch_to
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

    thread_t *best_thread = NULL;
    int highest_prio = -1;
    sched_class_t best_class = SCHED_IDLE;

    // Scan for best thread to run
    for (int i = 0; i < MAX_THREADS; i++) {
        if (threads[i].tid == -1 || threads[i].state != THREAD_READY) continue;

        bool better = false;
        if (!best_thread) {
            better = true;
        } else if (threads[i].sched_class < best_class) {
            better = true;
        } else if (threads[i].sched_class == best_class) {
            if (threads[i].priority > highest_prio) {
                better = true;
            }
        }

        if (better) {
            best_thread = &threads[i];
            highest_prio = threads[i].priority;
            best_class = threads[i].sched_class;
        }
    }

    if (!best_thread) {
        // If current thread is still running and nothing better found, keep it
        if (current_thread && current_thread->state == THREAD_RUNNING) return;
        // If blocked and nothing else to run... we have an idle problem.
        // For now, just return. 
        return;
    }

    // If current thread is still the best, and it's running, no switch
    if (best_thread == current_thread && current_thread && current_thread->state == THREAD_RUNNING) return;

    // Context Switch
    thread_t *prev = current_thread;
    thread_t *next = best_thread;
    
    if (prev && prev->state == THREAD_RUNNING) prev->state = THREAD_READY;
    next->state = THREAD_RUNNING;
    
    current_thread = next;
    current_process = current_thread->proc;
    
    set_kernel_stack(current_thread->kstack_ptr); 
    if (prev && prev != next) {
        switch_to(prev, next);
    }
}

int sched_get_current_tid(void) {
    if (current_thread) return current_thread->tid;
    return -1;
}

thread_t *sched_get_thread(int tid) {
    for (int i = 0; i < MAX_THREADS; i++) {
        if (threads[i].tid == tid) return &threads[i];
    }
    return NULL;
}

void sched_set_priority(int tid, sched_class_t cls, int prio) {

    thread_t *t = sched_get_thread(tid);

    if (!t) return;

    t->sched_class = cls;

    t->priority = prio;

    t->base_priority = prio;

}



void sched_sleep(void *chan) {

    if (!current_thread) return;

    current_thread->wait_chan = chan;

    current_thread->state = THREAD_BLOCKED;

    sched_yield();

}



void sched_wakeup(void *chan) {



    sched_wakeup_n(chan, -1); // -1 means wake all



}







void sched_wakeup_n(void *chan, int n) {



    int woken = 0;



    for (int i = 0; i < MAX_THREADS; i++) {



        if (threads[i].tid != -1 && threads[i].state == THREAD_BLOCKED && threads[i].wait_chan == chan) {



            threads[i].state = THREAD_READY;



            threads[i].wait_chan = NULL;



            woken++;



            if (n > 0 && woken >= n) break;



        }



    }



}




