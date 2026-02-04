#include <drivers/video/fb.h>
#include <kern/console.h>
#include <kern/cmdline.h>
#include <kern/time.h>
#include <sys/types.h>
#include <string.h>
#include <stdint.h>

extern void linear_fb_putpixel(int x, int y, uint32_t color);
extern fb_info_t fb;

// Mock framebuffer memory: small to ensure boot safety
#define MOCK_WIDTH 100
#define MOCK_HEIGHT 100
static uint8_t mock_fb_mem[MOCK_WIDTH * MOCK_HEIGHT * 4] __attribute__((aligned(4096)));

/* Read Time-Stamp Counter */
static inline uint64_t rdtsc(void) {
    uint32_t lo, hi;
    __asm__ volatile ("rdtsc" : "=a" (lo), "=d" (hi));
    return ((uint64_t)hi << 32) | lo;
}

void test_fb_perf(void) {
    kprint("\n=== Framebuffer Performance Tests ===\n");

    /* Part 1: FB Clear Performance Benchmark */
    kprint("\n--- FB Clear Performance Benchmark ---\n");
    // Backup existing fb state
    fb_info_t backup = fb;

    // Setup mock fb
    fb.addr = (uint32_t *)mock_fb_mem;
    fb.width = MOCK_WIDTH;
    fb.height = MOCK_HEIGHT;
    fb.bpp = 32;
    fb.pitch = MOCK_WIDTH * 4;
    fb.putpixel = linear_fb_putpixel;

    uint32_t colors[] = {0x00FF0000, 0x0000FF00, 0x000000FF, 0x00FFFFFF, 0x00000000};
    uint64_t start, end;
    uint64_t total_cycles = 0;
    int iterations = 10;

    // Warmup
    fb_clear(0);

    start = rdtsc();
    for (int i = 0; i < iterations; i++) {
        fb_clear(colors[i % 5]);
    }
    end = rdtsc();

    total_cycles = end - start;
    uint64_t avg_cycles = total_cycles / iterations;

    kprint("Resolution: 100x100 (Mock), 32bpp\n");
    extern int kprintf(const char *fmt, ...);
    kprintf("Clear iterations: %d\n", iterations);

    uint32_t avg_k = (uint32_t)(avg_cycles / 1000);
    kprintf("Average cycles per clear: %u Kcycles\n", avg_k);

    // Verify last color
    uint32_t expected = colors[(iterations - 1) % 5];
    uint32_t *p = (uint32_t *)mock_fb_mem;

    if (p[0] != expected) {
        kprintf("ERROR: Verification failed at [0]. Expected %x, Got %x\n", expected, p[0]);
    } else if (p[MOCK_WIDTH * MOCK_HEIGHT - 1] != expected) {
         kprintf("ERROR: Verification failed at end. Expected %x, Got %x\n", expected, p[MOCK_WIDTH * MOCK_HEIGHT - 1]);
    } else {
        kprint("Verification PASS.\n");
    }

    // Restore fb
    fb = backup;

    /* Part 2: Framebuffer Scroll Performance Test */
    kprint("\n--- Framebuffer Scroll Performance Test ---\n");

    /* Warm up */
    for (int i = 0; i < 50; i++) {
        kprint("\n");
    }

    start = rdtsc();
    iterations = 1000;

    for (int i = 0; i < iterations; i++) {
        kprint("\n");
    }

    end = rdtsc();
    total_cycles = end - start;
    avg_cycles = total_cycles / iterations;

    kprintf("Scrolled %d lines in %llu cycles (Avg: %llu cycles/line)\n", iterations, total_cycles, avg_cycles);
    kprint("=== End Tests ===\n");
}
