/*
 * percpu.c - Per-CPU Data Structures
 * 
 * Provides CPU-local storage via CPU-indexed percpu arrays on i386.
 * The kernel keeps %gs available for user/TLS contracts, especially the
 * Linux personality path.
 */

#include <stdio.h>
#include <string.h>

#include <kern/console.h>
#include <arch/i386/gdt.h>
#include <arch/i386/percpu.h>
#include <arch/i386/smp.h>
#include <arch/x86-common/lapic.h>

// Per-CPU data array
static struct percpu_data percpu_data_array[MAX_CPUS] __attribute__((aligned(64)));

// Get current CPU's percpu data (via LAPIC ID for now)
struct percpu_data *percpu_get(void) {
    uint32_t lapic_id = lapic_get_id();
    
    // Find CPU index by LAPIC ID
    for (int i = 0; i < cpu_count; i++) {
        if (cpus[i].lapic_id == lapic_id) {
            return &percpu_data_array[i];
        }
    }
    
    // Fallback to BSP
    return &percpu_data_array[0];
}

// Get percpu data for specific CPU
struct percpu_data *percpu_get_cpu(int cpu_id) {
    if (cpu_id < 0 || cpu_id >= MAX_CPUS) return NULL;
    return &percpu_data_array[cpu_id];
}

// Initialize per-CPU data for current CPU
void percpu_init_cpu(int cpu_id) {
    if (cpu_id < 0 || cpu_id >= MAX_CPUS) return;
    
    struct percpu_data *pcpu = &percpu_data_array[cpu_id];
    memset(pcpu, 0, sizeof(*pcpu));
    
    pcpu->cpu_id = (uint32_t)cpu_id;
    
    // Get LAPIC ID
    if (cpu_id < cpu_count) {
        pcpu->lapic_id = cpus[cpu_id].lapic_id;
    }
    
    // Initialize spinlock
    pcpu->lock = 0;
    
    // Initialize scheduler runqueue
    pcpu->runqueue_head = NULL;
    pcpu->runqueue_tail = NULL;
    pcpu->runqueue_count = 0;
    
    /* Single kprintf so the whole line emits atomically under the console
     * output lock -- APs bringing up concurrently would otherwise interleave. */
    kprintf("PERCPU: Initialized CPU %d\n", cpu_id);
}

// Initialize per-CPU subsystem
void percpu_init(void) {
    kprint("PERCPU: Initializing per-CPU data...\n");
    
    // Initialize BSP first
    percpu_init_cpu(0);
    
    // APs will initialize themselves when they boot
}

// Get current CPU ID (fast path via percpu)
int percpu_get_cpu_id(void) {
    struct percpu_data *pcpu = percpu_get();
    return (int)pcpu->cpu_id;
}

/*
 * Per-CPU current thread / process slots.
 *
 * current_thread / current_process (see <sys/proc.h>) are macros expanding to
 * *curthread_slot() / *curproc_slot(), so the running CPU always reads and
 * writes its own slot instead of a shared global (which two CPUs would clobber
 * on SMP -- the classic "rw_wunlock by non-owner" fork failure).  The
 * uniprocessor fast path returns CPU 0's slot directly with no LAPIC access;
 * only a real SMP system pays for percpu_get()'s CPU lookup.
 */
struct thread **curthread_slot(void) {
    if (cpu_count <= 1)
        return &percpu_data_array[0].current;
    return &percpu_get()->current;
}

struct process **curproc_slot(void) {
    if (cpu_count <= 1)
        return &percpu_data_array[0].current_proc;
    return &percpu_get()->current_proc;
}
