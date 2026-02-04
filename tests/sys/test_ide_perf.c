#include <stdint.h>
#include <kern/console.h>
#include <arch/x86-common/include/io.h>

/*
 * Naive implementation mirroring the original IDE driver code
 */
static void naive_read_loop(uint16_t port, void *buffer, int count) {
    uint16_t *buf = (uint16_t *)buffer;
    for (int j = 0; j < count; j++) {
        *buf++ = inw(port);
    }
}

/*
 * Optimized implementation using rep insw
 */
static void optimized_read_loop(uint16_t port, void *buffer, int count) {
    insw(port, buffer, count);
}

static inline uint64_t rdtsc(void) {
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

void test_ide_perf(void) {
    kprint("\n=== IDE PIO Performance Benchmark ===\n");

    /* Use a stack buffer */
    uint16_t buffer[256];
    /* Port 0x80 is commonly used for I/O delay, safe to read */
    uint16_t port = 0x80;
    int iterations = 1000;

    uint64_t start, end;
    uint64_t total_naive = 0;
    uint64_t total_opt = 0;

    /* Warmup */
    naive_read_loop(port, buffer, 256);
    optimized_read_loop(port, buffer, 256);

    /* Measure Naive */
    for (int i = 0; i < iterations; i++) {
        start = rdtsc();
        naive_read_loop(port, buffer, 256);
        end = rdtsc();
        total_naive += (end - start);
    }

    /* Measure Optimized */
    for (int i = 0; i < iterations; i++) {
        start = rdtsc();
        optimized_read_loop(port, buffer, 256);
        end = rdtsc();
        total_opt += (end - start);
    }

    uint64_t avg_naive = total_naive / iterations;
    uint64_t avg_opt = total_opt / iterations;

    kprintf("Naive (loop of inw): %llu cycles/sector\n", (unsigned long long)avg_naive);
    kprintf("Optimized (insw)   : %llu cycles/sector\n", (unsigned long long)avg_opt);

    if (avg_opt < avg_naive) {
        uint64_t improvement = avg_naive / avg_opt;
        kprintf("Improvement        : ~%llux faster\n", (unsigned long long)improvement);
    } else {
        kprint("Improvement        : None (or slower)\n");
    }

    kprint("=== End Benchmark ===\n");
}
