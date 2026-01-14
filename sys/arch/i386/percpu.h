#ifndef _ARCH_I386_PERCPU_H
#define _ARCH_I386_PERCPU_H

#include <stdint.h>

// Forward declaration
struct thread;

// Per-CPU data structure
struct percpu_data {
    // Identification
    uint32_t cpu_id;            // CPU index (0 = BSP)
    uint32_t lapic_id;          // Local APIC ID
    
    // Current execution context
    struct thread *current;     // Currently running thread
    struct thread *idle;        // Idle thread for this CPU
    
    // Scheduler runqueue (per-CPU)
    struct thread *runqueue_head;
    struct thread *runqueue_tail;
    uint32_t runqueue_count;
    
    // Statistics
    uint64_t ticks;             // Timer ticks since boot
    uint64_t idle_ticks;        // Ticks spent idle
    
    // Locking
    volatile uint32_t lock;     // Per-CPU lock
    
    // Padding to cache line boundary
    uint8_t _pad[16];
} __attribute__((aligned(64)));

// Get current CPU's per-CPU data
struct percpu_data *percpu_get(void);

// Get per-CPU data for specific CPU
struct percpu_data *percpu_get_cpu(int cpu_id);

// Initialize per-CPU data for a CPU
void percpu_init_cpu(int cpu_id);

// Initialize per-CPU subsystem
void percpu_init(void);

// Get current CPU ID (fast path)
int percpu_get_cpu_id(void);

// Convenience macros
#define THIS_CPU()          percpu_get()
#define CPU_ID()            percpu_get_cpu_id()
#define CURRENT_THREAD()    (percpu_get()->current)

#endif /* _ARCH_I386_PERCPU_H */
