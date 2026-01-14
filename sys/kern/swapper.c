/*
 * swapper.c - Kernel Process (PID 0) / Idle Thread
 * 
 * The kernel runs as PID 0 (swapper/idle).
 * Handles pageout daemon work and idle loop.
 * Ensures valid process context always exists.
 */

#include <kern/sched.h>
#include <sys/proc.h>
#include <stdint.h>

// The swapper process (PID 0)
static process_t swapper_proc;
static thread_t swapper_thread;

// Flag to indicate idle work is needed
static volatile int idle_work_pending = 0;

// Forward declarations
extern void vm_pageout_daemon(void);
extern void uma_reclaim(void);

// Initialize the swapper process (PID 0)
void swapper_init(void) {
    extern void *memset(void *s, int c, size_t n);
    
    // Initialize swapper process
    memset(&swapper_proc, 0, sizeof(swapper_proc));
    swapper_proc.pid = 0;
    swapper_proc.ppid = 0;
    swapper_proc.pgrp = 0;
    swapper_proc.session = 0;
    swapper_proc.is_kernel_task = 1;
    
    // Set comm name
    const char *name = "swapper";
    for (int i = 0; name[i] && i < AC_COMM_LEN - 1; i++) {
        swapper_proc.comm[i] = name[i];
    }
    
    // Initialize swapper thread (TID 0)
    memset(&swapper_thread, 0, sizeof(swapper_thread));
    swapper_thread.tid = 0;
    swapper_thread.proc = &swapper_proc;
    swapper_thread.state = THREAD_RUNNING;
    swapper_thread.sched_class = SCHED_IDLE;
    swapper_thread.priority = 0;
    swapper_thread.base_priority = 0;
    
    // Set as current (BSP starts as swapper)
    extern thread_t *current_thread;
    extern process_t *current_process;
    if (!current_thread) {
        current_thread = &swapper_thread;
        current_process = &swapper_proc;
    }
}

// Get the swapper process
process_t *swapper_get_proc(void) {
    return &swapper_proc;
}

// Get the idle thread for current CPU
thread_t *swapper_get_idle_thread(void) {
    // For SMP, each CPU has its own idle thread
    // For now, return the BSP's idle thread
    return &swapper_thread;
}

// Request idle work (called when memory pressure, etc.)
void swapper_request_work(void) {
    idle_work_pending = 1;
}

// Idle loop - runs when no other threads are ready
// NEVER returns
void swapper_idle_loop(void) {
    for (;;) {
        // 1. Check for work to do
        if (idle_work_pending) {
            idle_work_pending = 0;
            
            // Run pageout if memory pressure
            // vm_pageout_daemon();  // TODO: implement
            
            // Reclaim UMA caches
            // uma_reclaim();  // Already implemented
        }
        
        // 2. Try to find runnable thread
        extern thread_t *sched_idle_balance(void);
        thread_t *next = sched_idle_balance();
        if (next) {
            // Switch to found thread
            extern void sched_yield(void);
            sched_yield();
            continue;
        }
        
        // 3. Nothing to do - halt until interrupt
        // Check needs_resched before halting
        extern thread_t *current_thread;
        if (current_thread && current_thread->needs_resched) {
            continue;
        }
        
        // Enable interrupts and halt
        __asm__ volatile(
            "sti\n"
            "hlt\n"
        );
        
        // After waking from HLT, loop again
    }
}

// Ensure we never switch to NULL context
// Returns valid thread (current or idle)
thread_t *sched_ensure_context(void) {
    extern thread_t *current_thread;
    
    if (current_thread) {
        return current_thread;
    }
    
    // No current thread - use idle thread
    return swapper_get_idle_thread();
}

// Called before potentially blocking operation
// Ensures valid context for interrupt handling
void sched_enter_critical(void) {
    // Make sure we have a valid thread context
    sched_ensure_context();
}

// Check if current thread is the idle thread
int sched_is_idle(void) {
    extern thread_t *current_thread;
    if (!current_thread) return 1;
    return current_thread == &swapper_thread;
}
