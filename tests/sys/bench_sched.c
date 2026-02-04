#include <kern/sched.h>
#include <kern/console.h>
#include <stdint.h>

// Manually declare since headers don't expose it to userspace/tests cleanly
// But we are in kernel space tests. It should be in sched.h?
// It seems sched_affinity.c is a separate module.
uint32_t sched_get_affinity(int tid);

static inline uint64_t rdtsc(void) {
    uint32_t lo, hi;
    __asm__ volatile ("rdtsc" : "=a" (lo), "=d" (hi));
    return ((uint64_t)hi << 32) | lo;
}

void run_sched_bench(void) {
    kprintf("\n=== SCHEDULER BENCHMARK ===\n");

    // Warmup
    sched_get_affinity(1);

    uint64_t start = rdtsc();

    // Perform 1,000,000 lookups for a non-existent thread
    // This forces a full linear scan in the naive implementation
    // and a single hash check in the optimized implementation.
    int count = 1000000;
    volatile int dummy = 0;
    for (int i = 0; i < count; i++) {
        int res = sched_get_affinity(99999);
        if (res == 0) dummy++; // Prevent optimization (though func call barrier helps)
    }

    uint64_t end = rdtsc();
    uint64_t diff = end - start;

    // kprintf might not support %llu, so we split it or cast
    kprintf("Benchmark loops: %d\n", count);
    kprintf("Total Cycles: %u\n", (uint32_t)diff);
    kprintf("Total Cycles (High): %u\n", (uint32_t)(diff >> 32));
    kprintf("Cycles per op: %u\n", (uint32_t)(diff / count));
}
