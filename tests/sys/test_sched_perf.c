#include <kern/sched.h>
#include <drivers/console/console.h>
#include <sys/types.h>

extern thread_t threads[];

static inline uint64_t rdtsc(void) {
    uint32_t lo, hi;
    __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

// Replica of old sched_affinity.c logic (Linear Scan) for comparison
uint32_t sched_get_affinity_linear(int tid) {
    thread_t *t = NULL;
    for (int i = 0; i < MAX_THREADS; i++) {
        if (threads[i].tid == tid) {
            t = &threads[i];
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
