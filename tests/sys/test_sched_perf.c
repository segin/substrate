#include <kern/sched.h>
#include <drivers/console/console.h>
#include <sys/types.h>
/* The kernel's global process/thread tables are gone, and MAX_PROCS /
 * MAX_THREADS went with them; anything sized by them here is this file's
 * own storage.  Values match the other host tests. */
#ifndef MAX_THREADS
#define MAX_THREADS 64
#endif



static inline uint64_t rdtsc(void) {
    uint32_t lo, hi;
    __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

// Replica of old sched_affinity.c logic (Linear Scan) for comparison
uint32_t sched_get_affinity_linear(int tid) {
    /*
     * #425: this replicated the OLD lookup, a linear scan over the static
     * threads[MAX_THREADS] array -- which no longer exists.  The point of the
     * benchmark is linear-scan vs O(1), so scan the registry list instead:
     * still O(n) over the same population, and it compiles against the
     * kernel as it is rather than as it was.
     */
    thread_t *t = NULL;
    for (thread_t *it = thread_first(); it != NULL; it = thread_next(it)) {
        if (it->tid == tid) {
            t = it;
            break;
        }
    }

    if (!t) return 0;
    if (t->cpu_affinity == 0) return 0xFFFFFFFF;
    return t->cpu_affinity;
}

// Optimized version utilizing sched_get_thread (which is now O(1))
uint32_t sched_get_affinity_via_func(int tid) {
    thread_t *t = sched_get_thread(tid);

    if (!t) return 0;
    if (t->cpu_affinity == 0) return 0xFFFFFFFF;
    return t->cpu_affinity;
}

void run_sched_perf_tests(void) {
    kprint("Running Scheduler Performance Tests...\n");

    // Test with a TID that likely requires full scan (or doesn't exist)
    // This demonstrates worst-case performance difference.
    int tid = 9999;
    uint64_t start, end;
    volatile uint32_t mask;
    int iterations = 1000000;

    // Baseline (Linear Scan) - Worst Case
    start = rdtsc();
    for (int i = 0; i < iterations; i++) {
        mask = sched_get_affinity_linear(tid);
    }
    end = rdtsc();
    kprintf("Linear Scan Worst Case (1M): %u cycles\n", (uint32_t)(end - start));

    // Target (sched_get_thread) - Worst Case
    start = rdtsc();
    for (int i = 0; i < iterations; i++) {
        mask = sched_get_affinity_via_func(tid);
    }
    end = rdtsc();
    kprintf("Via sched_get_thread Worst Case (1M): %u cycles\n", (uint32_t)(end - start));
    (void)mask;
}
