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

static void test_strlen(void) {
    if (strlen("") != 0) {
        kprint("FAIL: strlen(\"\") != 0\n");
        failed_tests++;
    }
    if (strlen("a") != 1) {
        kprint("FAIL: strlen(\"a\") != 1\n");
        failed_tests++;
    }
    if (strlen("hello") != 5) {
        kprint("FAIL: strlen(\"hello\") != 5\n");
        failed_tests++;
    }
}

static void test_strcpy(void) {
    char src[] = "Hello World";
    char dest[20];

    memset(dest, 'X', sizeof(dest)); // Fill with garbage

    char *ret = strcpy(dest, src);

    if (ret != dest) {
        kprint("FAIL: strcpy return value mismatch\n");
        failed_tests++;
    }
    if (strcmp(dest, src) != 0) {
        kprint("FAIL: strcpy content mismatch\n");
        failed_tests++;
    }
    if (dest[strlen(src)] != '\0') {
         kprint("FAIL: strcpy null terminator missing\n");
         failed_tests++;
    }
    if (dest[sizeof(dest)-1] != 'X') {
         kprint("FAIL: strcpy buffer overflow check failed\n");
         failed_tests++;
    }
}

static void test_strncpy(void) {
    char src[] = "Hello";
    char dest[20];

    // Case 1: n > strlen(src) -> Pad with nulls
    memset(dest, 'X', sizeof(dest));
    char *ret = strncpy(dest, src, 10);

    if (ret != dest) {
        kprint("FAIL: strncpy return value mismatch\n");
        failed_tests++;
    }
    if (strcmp(dest, src) != 0) {
        kprint("FAIL: strncpy content mismatch\n");
        failed_tests++;
    }
    // Verify padding
    for (int i = 5; i < 10; i++) {
        if (dest[i] != '\0') {
            kprint("FAIL: strncpy padding failed\n");
            failed_tests++;
            break;
        }
    }
    if (dest[10] != 'X') {
        kprint("FAIL: strncpy overflow check failed\n");
        failed_tests++;
    }

    // Case 2: n < strlen(src) -> No null terminator
    memset(dest, 'X', sizeof(dest));
    strncpy(dest, src, 3);

    // Check first 3 chars match
    if (memcmp(dest, src, 3) != 0) {
        kprint("FAIL: strncpy truncation content mismatch\n");
        failed_tests++;
    }
    // Check NO null terminator at index 3
    if (dest[3] != 'X') {
        kprint("FAIL: strncpy should not null terminate if truncated\n");
        failed_tests++;
    }

    // Case 3: n == strlen(src) -> No null terminator
    memset(dest, 'X', sizeof(dest));
    strncpy(dest, src, 5);

    if (memcmp(dest, src, 5) != 0) {
        kprint("FAIL: strncpy exact length content mismatch\n");
        failed_tests++;
    }
    if (dest[5] != 'X') {
        kprint("FAIL: strncpy exact length should not null terminate\n");
        failed_tests++;
    }
}

static void test_memmove(void) {
    char buf[32];
    char expected[32];

    // Basic non-overlapping
    memset(buf, 0, sizeof(buf));
    strcpy(buf, "Hello World");
    memmove(buf + 20, buf, 12);
    ASSERT_MEM_EQ(buf + 20, "Hello World", 12, "memmove basic");

    // Overlap forward (dest < src)
    strcpy(buf, "12345678");
    strcpy(expected, "23456678");
    memmove(buf, buf + 1, 5);
    ASSERT_MEM_EQ(buf, expected, 8, "memmove overlap forward (dest < src)");

    // Overlap backward (dest > src)
    strcpy(buf, "12345678");
    strcpy(expected, "11234578");
    memmove(buf + 1, buf, 5);
    ASSERT_MEM_EQ(buf, expected, 8, "memmove overlap backward (dest > src)");

    // Exact overlap
    strcpy(buf, "12345678");
    memmove(buf, buf, 8);
    ASSERT_MEM_EQ(buf, "12345678", 8, "memmove exact overlap");

    // Zero size
    strcpy(buf, "12345678");
    memmove(buf, buf + 1, 0);
    ASSERT_MEM_EQ(buf, "12345678", 8, "memmove zero size");
}

static void test_memmove_comprehensive(void) {
    const int buffer_size = 256;
    char *buffer = kmalloc(buffer_size);
    char *control = kmalloc(buffer_size);

    if (!buffer || !control) {
        kprint("SKIP: test_memmove_comprehensive (OOM)\n");
        if (buffer) kfree(buffer, buffer_size);
        if (control) kfree(control, buffer_size);
        return;
    }

    // Initialize with a pattern
    for (int i = 0; i < buffer_size; i++) {
        buffer[i] = (char)(i & 0xFF);
        control[i] = (char)(i & 0xFF);
    }

    // Iterate through various src/dst offsets and lengths
    for (int src_off = 0; src_off < buffer_size - 16; src_off += 13) {
        for (int dst_off = 0; dst_off < buffer_size - 16; dst_off += 17) {
            for (int len = 0; len < 64; len++) {
                 // Reset buffer content for next iteration
                 for (int i = 0; i < buffer_size; i++) {
                     buffer[i] = (char)(i & 0xFF);
                     control[i] = (char)(i & 0xFF);
                 }

                 if (src_off + len > buffer_size || dst_off + len > buffer_size) continue;

                 // Perform operation on control buffer (using reference implementation via temp buffer)
                 char tmp[64];
                 memcpy(tmp, control + src_off, len);
                 memcpy(control + dst_off, tmp, len);

                 // Perform operation on test buffer
                 memmove(buffer + dst_off, buffer + src_off, len);

                 // Compare
                 if (memcmp(buffer, control, buffer_size) != 0) {
                     char msg[128];
                     snprintf(msg, sizeof(msg), "FAIL: memmove comprehensive mismatch at src=%d dst=%d len=%d\n", src_off, dst_off, len);
                     kprint(msg);
                     failed_tests++;
                     goto cleanup;
                 }
            }
        }
    }

cleanup:
    kfree(buffer, buffer_size);
    kfree(control, buffer_size);
}

static void test_memset_basic(void) {
    char buf[20];
    char expected[20];

    // Initialize with something else
    for (int i=0; i<20; i++) {
        buf[i] = (char)0xAA;
        expected[i] = (char)0xAA;
    }

    memset(buf, 0, 10);
    for (int i=0; i<10; i++) expected[i] = 0;

    ASSERT_MEM_EQ(buf, expected, 20, "Basic memset 0 failed");

    memset(buf, 0x55, 10);
    for (int i=0; i<10; i++) expected[i] = 0x55;

    ASSERT_MEM_EQ(buf, expected, 20, "Basic memset 0x55 failed");
}

static void test_memset_small(void) {
    char buf[16];
    char expected[16];

    for (int i = 0; i <= 8; i++) {
        // Reset buffers
        for (int j=0; j<16; j++) {
            buf[j] = 0xAA;
            expected[j] = 0xAA;
        }

        memset(buf, 0x77, i);
        for(int j=0; j<i; j++) expected[j] = 0x77;

        ASSERT_MEM_EQ(buf, expected, 16, "Small memset consistency check");
    }
}

static void test_memset_unaligned(void) {
    char buf[64];
    char expected[64];

    // Initialize
    for(int i=0; i<64; i++) {
        buf[i] = 0xAA;
        expected[i] = 0xAA;
    }

    // Unaligned start (offset 1)
    memset(buf + 1, 0xBB, 10);
    for(int i=0; i<10; i++) expected[1+i] = 0xBB;

    ASSERT_MEM_EQ(buf, expected, 64, "Unaligned start memset failed");

    // Unaligned start (offset 3)
    for(int i=0; i<64; i++) { buf[i] = 0xAA; expected[i] = 0xAA; }
    memset(buf + 3, 0xCC, 10);
    for(int i=0; i<10; i++) expected[3+i] = 0xCC;

    ASSERT_MEM_EQ(buf, expected, 64, "Unaligned start 3 memset failed");
}

static void test_memset_large(void) {
    size_t size = 4096;
    char *buf = kmalloc(size);
    char *expected = kmalloc(size);

    if (!buf || !expected) {
        kprint("SKIP: test_memset_large (OOM)\n");
        if (buf) kfree(buf, size);
        if (expected) kfree(expected, size);
        return;
    }

    // Fill with pattern
    for(size_t i=0; i<size; i++) {
        buf[i] = 0xAA;
        expected[i] = 0xAA;
    }

    // Memset entire buffer
    memset(buf, 0xDD, size);
    for(size_t i=0; i<size; i++) expected[i] = 0xDD;

    ASSERT_MEM_EQ(buf, expected, size, "Large memset failed");

    kfree(buf, size);
    kfree(expected, size);
}

static void test_strcmp(void) {
    if (strcmp("", "") != 0) {
        kprint("FAIL: strcmp(\"\", \"\") != 0\n");
        failed_tests++;
    }
    if (strcmp("a", "a") != 0) {
        kprint("FAIL: strcmp(\"a\", \"a\") != 0\n");
        failed_tests++;
    }
    if (strcmp("abc", "abc") != 0) {
        kprint("FAIL: strcmp(\"abc\", \"abc\") != 0\n");
        failed_tests++;
    }
    if (strcmp("abc", "abd") >= 0) {
        kprint("FAIL: strcmp(\"abc\", \"abd\") >= 0\n");
        failed_tests++;
    }
    if (strcmp("abd", "abc") <= 0) {
        kprint("FAIL: strcmp(\"abd\", \"abc\") <= 0\n");
        failed_tests++;
    }
    if (strcmp("abc", "abcd") >= 0) {
        kprint("FAIL: strcmp(\"abc\", \"abcd\") >= 0\n");
        failed_tests++;
    }
    if (strcmp("abcd", "abc") <= 0) {
        kprint("FAIL: strcmp(\"abcd\", \"abc\") <= 0\n");
        failed_tests++;
    }
}

static void test_strncmp(void) {
    // n=0 cases
    if (strncmp("", "", 0) != 0) {
        kprint("FAIL: strncmp(\"\", \"\", 0) != 0\n");
        failed_tests++;
    }
    if (strncmp("abc", "def", 0) != 0) {
        kprint("FAIL: strncmp(\"abc\", \"def\", 0) != 0\n");
        failed_tests++;
    }

    // Equal strings
    if (strncmp("abc", "abc", 3) != 0) {
        kprint("FAIL: strncmp(\"abc\", \"abc\", 3) != 0\n");
        failed_tests++;
    }
    // n > length
    if (strncmp("abc", "abc", 5) != 0) {
        kprint("FAIL: strncmp(\"abc\", \"abc\", 5) != 0\n");
        failed_tests++;
    }

    // Difference after n (should appear equal)
    if (strncmp("abc", "abd", 2) != 0) {
        kprint("FAIL: strncmp(\"abc\", \"abd\", 2) != 0\n");
        failed_tests++;
    }

    // Difference within n
    if (strncmp("abc", "abd", 3) >= 0) {
        kprint("FAIL: strncmp(\"abc\", \"abd\", 3) >= 0\n");
        failed_tests++;
    }
    if (strncmp("abd", "abc", 3) <= 0) {
        kprint("FAIL: strncmp(\"abd\", \"abc\", 3) <= 0\n");
        failed_tests++;
    }

    // Prefix
    if (strncmp("abc", "abcd", 3) != 0) {
         kprint("FAIL: strncmp(\"abc\", \"abcd\", 3) != 0\n");
         failed_tests++;
    }
    if (strncmp("abc", "abcd", 4) >= 0) {
         kprint("FAIL: strncmp(\"abc\", \"abcd\", 4) >= 0\n");
         failed_tests++;
    }
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

    kprint("Checking strlen correctness...\n");
    test_strlen();

    kprint("Checking strcpy correctness...\n");
    test_strcpy();

    kprint("Checking strncpy correctness...\n");
    test_strncpy();

    kprint("Checking strcmp correctness...\n");
    test_strcmp();

    kprint("Checking strncmp correctness...\n");
    test_strncmp();

    kprint("Checking memcpy correctness...\n");
    test_memcpy_basic();
    test_memcpy_small();
    test_memcpy_unaligned();
    test_memcpy_large();

    kprint("Checking memmove correctness...\n");
    test_memmove();
    test_memmove_comprehensive();

    kprint("Checking memset correctness...\n");
    test_memset_basic();
    test_memset_small();
    test_memset_unaligned();
    test_memset_large();

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
