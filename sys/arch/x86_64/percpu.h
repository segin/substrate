/*
 * percpu.h - x86_64 per-CPU data
 *
 * The same interface as arch/i386/percpu.h (the code is
 * arch/x86-common/percpu.c); the GDT and TSS live in gdt.c here instead
 * of in this structure.
 */
#ifndef _ARCH_X86_64_PERCPU_H
#define _ARCH_X86_64_PERCPU_H

#include <stdint.h>

struct thread;
struct process;

struct percpu_data {
    uint32_t cpu_id;            // CPU index (0 = BSP)
    uint32_t lapic_id;          // Local APIC ID

    // Current execution context
    struct thread *current;     // Currently running thread
    struct process *current_proc; // Its process (current_process slot)
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
} __attribute__((aligned(64)));

struct percpu_data *percpu_get(void);
struct percpu_data *percpu_get_cpu(int cpu_id);
void percpu_init_cpu(int cpu_id);
void percpu_init(void);
int percpu_get_cpu_id(void);

#define THIS_CPU()          percpu_get()
#define CPU_ID()            percpu_get_cpu_id()
#define CURRENT_THREAD()    (percpu_get()->current)

#endif /* _ARCH_X86_64_PERCPU_H */
