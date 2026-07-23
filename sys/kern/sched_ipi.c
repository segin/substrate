/*
 * sched_ipi.c - IPI-based Scheduler Preemption
 * 
 * Uses Inter-Processor Interrupts to preempt threads on remote CPUs.
 */

#include <stdint.h>

#include <sys/proc.h>
#include <kern/sched.h>
#include <kern/runqueue.h>
#include <arch/i386/percpu.h>
#include <arch/i386/smp.h>
#include <arch/x86-common/lapic.h>

/* IPI vector defined in sched.h */

// Number of CPUs

// Send preemption IPI to a specific CPU
void sched_send_preempt_ipi(int cpu_id) {
    if (cpu_id < 0 || cpu_id >= num_cpus || cpu_id >= MAX_CPUS) return;

    // Translate the logical CPU index to its real per-core LAPIC id.
    lapic_send_ipi(cpus[cpu_id].lapic_id, SCHED_IPI_VECTOR);
}

// Send preemption IPI to all other CPUs
void sched_send_preempt_all(void) {
    lapic_send_ipi_all_excl_self(SCHED_IPI_VECTOR);
}

// Request reschedule on a specific CPU
void sched_resched_cpu(int cpu_id) {
    int my_cpu = percpu_get_cpu_id();
    
    if (cpu_id == my_cpu) {
        // Local reschedule - just set flag
        if (current_thread) {
            current_thread->needs_resched = 1;
        }
    } else {
        // Remote reschedule - send IPI
        sched_send_preempt_ipi(cpu_id);
    }
}

// IPI handler for scheduler preemption
// Called from IDT handler for SCHED_IPI_VECTOR
void sched_ipi_handler(void) {
    
    // Mark current thread as needing reschedule
    if (current_thread) {
        current_thread->needs_resched = 1;
    }
    
    // Send EOI (required for LAPIC)
    lapic_send_eoi();
}

// Check if current thread needs reschedule
int sched_needs_resched(void) {
    if (!current_thread) return 0;
    return current_thread->needs_resched != 0;
}

// Clear reschedule flag
void sched_clear_resched(void) {
    if (current_thread) {
        current_thread->needs_resched = 0;
    }
}

// Preempt current thread if higher priority thread is ready
void sched_preempt_check(void) {
    if (!current_thread) return;
    

    
    void *rq = sched_get_current_runqueue();
    if (!rq) return;
    
    thread_t *next = runqueue_peek(rq);
    if (!next) return;
    
    // Compare priorities
    // Lower sched_class number = higher priority (RT < TS < IDLE)
    int should_preempt = 0;
    
    if (next->sched_class < current_thread->sched_class) {
        should_preempt = 1;
    } else if (next->sched_class == current_thread->sched_class) {
        // Within same class, check priority
        if (next->priority < current_thread->priority) {
            should_preempt = 1;
        }
    }
    
    if (should_preempt) {
        current_thread->needs_resched = 1;
    }
}

// Register the IPI handler in the IDT
// This should be called during kernel initialization
void sched_ipi_init(void) {
    // Register handler in IDT
    // extern void idt_register_handler(int vector, void (*handler)(void));
    // idt_register_handler(SCHED_IPI_VECTOR, sched_ipi_handler);
    
    // Note: Actual IDT registration depends on arch-specific code
    // The handler needs to be wrapped in assembly to save/restore context
}
