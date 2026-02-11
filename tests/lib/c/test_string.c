#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>

/*
 * Declare the prefixed memcpy function we are testing.
 * The implementation comes from lib/c/src/string.c, compiled with symbol prefixing.
 */
void *libc_memcpy(void *dest, const void *src, size_t n);

static int failed_tests = 0;

#define ASSERT_MEM_EQ(a, b, size, msg) do { \
    if (memcmp(a, b, size) != 0) { \
        printf("FAIL: %s\n", msg); \
        failed_tests++; \
    } \
} while(0)

// 64KB buffer for testing
// Wrappers for external symbols renamed by objcopy
int libc_rand(void) {
    return rand();
}

void *libc_malloc(size_t size) {
    return malloc(size);
}

static void test_memcpy_basic(void) {
    char src[] = "Hello World";
    char dest[20] = {0};

    libc_memcpy(dest, src, 12); // Include null terminator
    ASSERT_MEM_EQ(dest, src, 12, "Basic memcpy failed");
}

static void test_memcpy_small(void) {
    char src[] = "12345678";
    char dest[10];

    for (int i = 0; i <= 8; i++) {
        memset(dest, 0, sizeof(dest));
        libc_memcpy(dest, src, i);
        if (memcmp(dest, src, i) != 0) {
            printf("FAIL: Small memcpy consistency check (size %d)\n", i);
            failed_tests++;
        }
    }
}

static void test_memcpy_unaligned(void) {
    char s_buf[64] __attribute__((aligned(16)));
    char d_buf[64] __attribute__((aligned(16)));

    for (int i = 0; i < 64; i++) s_buf[i] = (char)i;

    // Unaligned dest (offset 1)
    memset(d_buf, 0, 64);
    libc_memcpy(d_buf + 1, s_buf, 10);
    ASSERT_MEM_EQ(d_buf + 1, s_buf, 10, "Unaligned dest memcpy failed");

    // Unaligned src (offset 1)
    memset(d_buf, 0, 64);
    libc_memcpy(d_buf, s_buf + 1, 10);
    ASSERT_MEM_EQ(d_buf, s_buf + 1, 10, "Unaligned src memcpy failed");

    // Both unaligned
    memset(d_buf, 0, 64);
    libc_memcpy(d_buf + 1, s_buf + 1, 10);
    ASSERT_MEM_EQ(d_buf + 1, s_buf + 1, 10, "Both unaligned memcpy failed");
}

static void test_memcpy_large(void) {
    size_t size = 4096;
    // Using malloc here instead of kmalloc as this is a hosted test
    char *src = malloc(size);
    char *dest = malloc(size);

    if (!src || !dest) {
        printf("SKIP: test_memcpy_large (OOM)\n");
        if (src) free(src);
        if (dest) free(dest);
        return;
    }

    for (size_t i = 0; i < size; i++) src[i] = (char)(i & 0xFF);
    memset(dest, 0, size);

    libc_memcpy(dest, src, size);
    ASSERT_MEM_EQ(dest, src, size, "Large memcpy failed");

    free(src);
    free(dest);
}

int main(void) {
    printf("\n=== LIBC STRING TESTS (memcpy) ===\n");

    test_memcpy_basic();
    test_memcpy_small();
    test_memcpy_unaligned();
    test_memcpy_large();

    if (failed_tests == 0) {
        printf("All tests passed!\n");
        return 0;
    } else {
        printf("%d tests failed.\n", failed_tests);
        return 1;
    }
}
