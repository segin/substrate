#include <kern/console.h>
#include <string.h>
#include <stdint.h>
#include "tests.h"

extern int snprintf(char *str, size_t size, const char *format, ...);

// RDTSC wrapper for cycle counting
static inline uint64_t rdtsc(void) {
    uint32_t lo, hi;
    __asm__ __volatile__ ("rdtsc" : "=a" (lo), "=d" (hi));
    return ((uint64_t)hi << 32) | lo;
}

// 64KB buffer should be enough for testing up to large sizes
#define BUF_SIZE (64 * 1024)
static uint8_t src_buf[BUF_SIZE] __attribute__((aligned(16)));
static uint8_t dst_buf[BUF_SIZE] __attribute__((aligned(16)));

static void test_memcpy_correctness(void) {
    kprint("Checking memcpy correctness... ");

    // Test 1: Simple copy
    memset(src_buf, 0xAA, 128);
    memset(dst_buf, 0x00, 128);
    memcpy(dst_buf, src_buf, 128);
    if (memcmp(src_buf, dst_buf, 128) != 0) {
        kprint("FAIL (Simple copy)\n");
        return;
    }

    // Test 2: Unaligned copy (src aligned, dst unaligned)
    memset(src_buf, 0xBB, 128);
    memset(dst_buf, 0x00, 128);
    memcpy(dst_buf + 1, src_buf, 127);
    if (memcmp(src_buf, dst_buf + 1, 127) != 0) {
        kprint("FAIL (Unaligned dst)\n");
        return;
    }

    // Test 3: Unaligned copy (src unaligned, dst aligned)
    memset(src_buf, 0xCC, 128);
    memset(dst_buf, 0x00, 128);
    memcpy(dst_buf, src_buf + 1, 127);
    if (memcmp(src_buf + 1, dst_buf, 127) != 0) {
        kprint("FAIL (Unaligned src)\n");
        return;
    }

    // Test 4: Both unaligned
    memset(src_buf, 0xDD, 128);
    memset(dst_buf, 0x00, 128);
    memcpy(dst_buf + 1, src_buf + 2, 120);
    if (memcmp(src_buf + 2, dst_buf + 1, 120) != 0) {
        kprint("FAIL (Both unaligned)\n");
        return;
    }

    kprint("PASS\n");
}

static void benchmark_memcpy(const char *label, void *dst, const void *src, size_t n, int iterations) {
    uint64_t start, end;
    uint64_t total_cycles = 0;

    // Warmup
    memcpy(dst, src, n);

    for (int i = 0; i < iterations; i++) {
        start = rdtsc();
        memcpy(dst, src, n);
        end = rdtsc();
        total_cycles += (end - start);
    }

    uint32_t avg = (uint32_t)(total_cycles / iterations);

    char msg[128];
    snprintf(msg, sizeof(msg), "  %s (%d bytes): %d cycles (avg)\n", label, (int)n, avg);
    kprint(msg);
}

void run_string_tests(void) {
    kprint("\n=== STRING TESTS ===\n");

    test_memcpy_correctness();

    kprint("Benchmarking memcpy:\n");

    // Initialize buffers to avoid page faults affecting first run too much (though warmup helps)
    memset(src_buf, 1, BUF_SIZE);
    memset(dst_buf, 0, BUF_SIZE);

    // Aligned
    benchmark_memcpy("Aligned 8", dst_buf, src_buf, 8, 1000);
    benchmark_memcpy("Aligned 64", dst_buf, src_buf, 64, 1000);
    benchmark_memcpy("Aligned 512", dst_buf, src_buf, 512, 1000);
    benchmark_memcpy("Aligned 4096", dst_buf, src_buf, 4096, 100);
    benchmark_memcpy("Aligned 64K", dst_buf, src_buf, 64*1024, 10);

    // Unaligned (dest offset 1)
    benchmark_memcpy("Unaligned Dst 8", dst_buf + 1, src_buf, 8, 1000);
    benchmark_memcpy("Unaligned Dst 64", dst_buf + 1, src_buf, 64, 1000);
    benchmark_memcpy("Unaligned Dst 4096", dst_buf + 1, src_buf, 4096, 100);

    // Unaligned (src offset 1)
    benchmark_memcpy("Unaligned Src 64", dst_buf, src_buf + 1, 64, 1000);
    benchmark_memcpy("Unaligned Src 4096", dst_buf, src_buf + 1, 4096, 100);

    kprint("=== STRING TESTS COMPLETE ===\n\n");
}
