#include <kern/console.h>
#include <drivers/video/fb.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

extern fb_info_t fb;

static __inline uint64_t rdtsc(void) {
    uint32_t lo, hi;
    __asm__ __volatile__ ("rdtsc" : "=a" (lo), "=d" (hi));
    return ((uint64_t)hi << 32) | lo;
}

void test_fb_perf(void) {
    kprint("FB Perf: Starting benchmark...\n");

    if (!fb.addr) {
        kprint("FB Perf: No framebuffer active. Skipping.\n");
        return;
    }

    uint32_t color = 0xFF0000FF; // Red
    uint64_t start, end;
    int iterations = 100;

    start = rdtsc();
    for (int i = 0; i < iterations; i++) {
        fb_clear(color);
    }
    end = rdtsc();

    uint64_t total_cycles = end - start;
    uint64_t avg_cycles = total_cycles / iterations;

    // Split 64-bit for printing if %llu is not supported
    uint32_t avg_lo = (uint32_t)avg_cycles;

    kprintf("FB Perf: Avg Cycles per Clear: %u\n", avg_lo);
}
