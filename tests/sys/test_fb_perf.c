#include <kern/console.h>
#include <kern/time.h>
#include <stdint.h>

/* Read Time-Stamp Counter */
static inline uint64_t rdtsc(void) {
    uint32_t lo, hi;
    __asm__ volatile ("rdtsc" : "=a" (lo), "=d" (hi));
    return ((uint64_t)hi << 32) | lo;
}

void test_fb_perf(void) {
    kprint("\n=== Framebuffer Scroll Performance Test ===\n");

    /* Warm up */
    for (int i = 0; i < 50; i++) {
        kprint("\n");
    }

    uint64_t start = rdtsc();
    int iterations = 1000;

    for (int i = 0; i < iterations; i++) {
        kprint("\n");
    }

    uint64_t end = rdtsc();
    uint64_t total = end - start;
    uint64_t avg = total / iterations;

    extern int kprintf(const char *fmt, ...);
    kprintf("Scrolled %d lines in %llu cycles (Avg: %llu cycles/line)\n", iterations, total, avg);
    kprint("=== End Test ===\n");
}
