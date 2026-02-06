#include <kern/sched.h>
#include <kern/runqueue.h>
#include <kern/console.h>
#include <stdint.h>
#include <string.h>

// External declarations for scheduler internals
extern int num_cpus;
extern void sched_smp_init(int cpu_count);
extern void sched_enqueue(thread_t *t);
extern void sched_dequeue(thread_t *t);
extern runqueue_t *sched_get_runqueue(int cpu_id);

static inline uint64_t rdtsc(void) {
    uint32_t lo, hi;
    __asm__ volatile ("rdtsc" : "=a" (lo), "=d" (hi));
    return ((uint64_t)hi << 32) | lo;
}

void run_sched_dequeue_bench(void) {
    kprintf("\n=== SCHED DEQUEUE BENCHMARK ===\n");

    // Save original state
    int orig_num_cpus = num_cpus;

    // Simulate SMP with MAX_CPUS (or 16 if MAX_CPUS is large)
    int bench_cpus = 16;
    if (bench_cpus > MAX_CPUS) bench_cpus = MAX_CPUS;

    kprintf("Simulating %d CPUs for benchmark...\n", bench_cpus);

    // Re-initialize scheduler to setup runqueues for all simulated CPUs
    // sched_smp_init is safe to call as it just inits the array
    sched_smp_init(bench_cpus);

    // Create a dummy thread
    thread_t dummy_thread;
    memset(&dummy_thread, 0, sizeof(dummy_thread));
    dummy_thread.tid = 12345;
    dummy_thread.priority = 10;
    dummy_thread.sched_class = SCHED_TIMESHARE;
    dummy_thread.cpu_affinity = 0; // Run on any CPU

    // Benchmark Loop
    int iterations = 100000; // 100k iterations

    // Warmup
    sched_enqueue(&dummy_thread);
    sched_dequeue(&dummy_thread);

    uint64_t start = rdtsc();

    for (int i = 0; i < iterations; i++) {
        // Enqueue (puts it on some queue)
        sched_enqueue(&dummy_thread);

        // Dequeue (this is what we want to optimize)
        sched_dequeue(&dummy_thread);
    }

    uint64_t end = rdtsc();
    uint64_t diff = end - start;

    // Restore original state
    // Note: We leave the runqueues initialized but reduce num_cpus back
    num_cpus = orig_num_cpus;

    kprintf("Benchmark iterations: %d\n", iterations);
    kprintf("Total Cycles: %u\n", (uint32_t)diff);
    kprintf("Cycles per op (enqueue+dequeue): %u\n", (uint32_t)(diff / iterations));
}
