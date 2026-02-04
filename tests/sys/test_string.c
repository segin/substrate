/*
 * Unit tests and benchmarks for string library functions
 */

#include <kern/console.h>
#include <string.h>
#include <stdint.h>
#include <vm/vm_kmem.h>
#include "tests.h"

extern int snprintf(char *str, size_t size, const char *format, ...);

static int failed_tests = 0;

#define ASSERT_MEM_EQ(a, b, size, msg) do { \
    if (memcmp(a, b, size) != 0) { \
        kprint("FAIL: "); kprint(msg); kprint("\n"); \
        failed_tests++; \
    } \
} while(0)

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

// Functional Tests (Correctness)

static void test_memcpy_basic(void) {
    char src[] = "Hello World";
    char dest[20] = {0};

    memcpy(dest, src, 12); // Include null terminator
    ASSERT_MEM_EQ(dest, src, 12, "Basic memcpy failed");
}

static void test_memcpy_small(void) {
    char src[] = "12345678";
    char dest[10];

    for (int i = 0; i <= 8; i++) {
        memset(dest, 0, sizeof(dest));
        memcpy(dest, src, i);
        if (memcmp(dest, src, i) != 0) {
            kprint("FAIL: Small memcpy consistency check\n");
            failed_tests++;
        }
    }
}

static void test_memcpy_unaligned(void) {
    char s_buf[64];
    char d_buf[64];

    for (int i = 0; i < 64; i++) s_buf[i] = (char)i;

    // Unaligned dest (offset 1)
    memset(d_buf, 0, 64);
    memcpy(d_buf + 1, s_buf, 10);
    ASSERT_MEM_EQ(d_buf + 1, s_buf, 10, "Unaligned dest memcpy failed");

    // Unaligned src (offset 1)
    memset(d_buf, 0, 64);
    memcpy(d_buf, s_buf + 1, 10);
    ASSERT_MEM_EQ(d_buf, s_buf + 1, 10, "Unaligned src memcpy failed");

    // Both unaligned
    memset(d_buf, 0, 64);
    memcpy(d_buf + 1, s_buf + 1, 10);
    ASSERT_MEM_EQ(d_buf + 1, s_buf + 1, 10, "Both unaligned memcpy failed");
}

static void test_memcpy_large(void) {
    size_t size = 4096;
    char *src = kmalloc(size);
    char *dest = kmalloc(size);

    if (!src || !dest) {
        kprint("SKIP: test_memcpy_large (OOM)\n");
        if (src) kfree(src, size);
        if (dest) kfree(dest, size);
        return;
    }

    for (size_t i = 0; i < size; i++) src[i] = (char)(i & 0xFF);
    memset(dest, 0, size);

    memcpy(dest, src, size);
    ASSERT_MEM_EQ(dest, src, size, "Large memcpy failed");

    kfree(src, size);
    kfree(dest, size);
}

// Performance Benchmarks

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
    failed_tests = 0;

    kprint("Checking memcpy correctness...\n");
    test_memcpy_basic();
    test_memcpy_small();
    test_memcpy_unaligned();
    test_memcpy_large();

    if (failed_tests == 0) {
        kprint("Correctness: PASS\n");
    } else {
        kprint("Correctness: FAIL\n");
    }

    kprint("Benchmarking memcpy:\n");

    // Initialize buffers
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
