#include <kern/console.h>
#include <stdint.h>
#include <stddef.h>

extern int snprintf(char *str, size_t size, const char *format, ...);

static inline uint64_t rdtsc(void) {
    uint32_t lo, hi;
    __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

void test_console_perf(void) {
    kprint("\n=== CONSOLE PERF TEST ===\n");

    // Warm up
    for(int i=0; i<50; i++) kprint("\n");

    uint64_t start = rdtsc();
    int lines = 1000;

    for (int i = 0; i < lines; i++) {
        kprint("Scrolling line test... 1234567890123456789012345678901234567890\n");
    }

    uint64_t end = rdtsc();
    uint64_t diff = end - start;

    char buf[128];
    uint32_t diff_hi = (uint32_t)(diff >> 32);
    uint32_t diff_lo = (uint32_t)(diff & 0xFFFFFFFF);

    // Average
    uint64_t avg = diff / lines;
    uint32_t avg_hi = (uint32_t)(avg >> 32);
    uint32_t avg_lo = (uint32_t)(avg & 0xFFFFFFFF);

    snprintf(buf, sizeof(buf), "Total: 0x%x%08x cycles for %d lines\n", diff_hi, diff_lo, lines);
    kprint(buf);

    snprintf(buf, sizeof(buf), "Avg:   0x%x%08x cycles/line\n", avg_hi, avg_lo);
    kprint(buf);
}
