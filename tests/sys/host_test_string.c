#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>

// Mock kernel functions
void kprint(const char *str) {
    printf("%s", str);
}

void *kmalloc(size_t size) {
    return malloc(size);
}

void kfree(void *ptr, size_t size) {
    (void)size;
    free(ptr);
}

// Rename functions to avoid conflict with host libc
#define memcpy kernel_memcpy
#define memset kernel_memset
#define memmove kernel_memmove
#define memcmp kernel_memcmp
#define strlen kernel_strlen
#define strcpy kernel_strcpy
#define strncpy kernel_strncpy
#define strcmp kernel_strcmp
#define strncmp kernel_strncmp
#define strchr kernel_strchr
#define strspn kernel_strspn
#define strpbrk kernel_strpbrk

// Include the source file directly
#include "../../sys/lib/string.c"

// Test Helper Macros
#define ASSERT_STREQ(a, b, msg) do { \
    if (strcmp(a, b) != 0) { \
        printf("FAIL: %s\n  Expected: '%s'\n  Actual:   '%s'\n", msg, b, a); \
        exit(1); \
    } \
} while(0)

#define ASSERT_MEM_EQ(a, b, n, msg) do { \
    if (memcmp(a, b, n) != 0) { \
        printf("FAIL: %s (memory mismatch)\n", msg); \
        exit(1); \
    } \
} while(0)

#define ASSERT_EQ(a, b, msg) do { \
    if ((a) != (b)) { \
        printf("FAIL: %s\n  Expected: %ld\n  Actual:   %ld\n", msg, (long)(b), (long)(a)); \
        exit(1); \
    } \
} while(0)

// Tests
void test_strcpy(void) {
    char src[] = "Hello World";
    char dest[20];

    memset(dest, 'X', sizeof(dest)); // Fill with garbage

    char *ret = kernel_strcpy(dest, src);

    ASSERT_EQ((uintptr_t)ret, (uintptr_t)dest, "strcpy return value");
    ASSERT_STREQ(dest, src, "strcpy content");
    ASSERT_EQ(dest[strlen(src)], '\0', "strcpy null terminator");
    ASSERT_EQ(dest[sizeof(dest)-1], 'X', "strcpy buffer overflow check");

    printf("test_strcpy: PASS\n");
}

void test_strcpy_empty(void) {
    char src[] = "";
    char dest[20];

    memset(dest, 'X', sizeof(dest));

    char *ret = kernel_strcpy(dest, src);

    ASSERT_EQ((uintptr_t)ret, (uintptr_t)dest, "strcpy empty return value");
    ASSERT_STREQ(dest, "", "strcpy empty content");
    ASSERT_EQ(dest[0], '\0', "strcpy empty null terminator");
    ASSERT_EQ(dest[1], 'X', "strcpy empty buffer check");

    printf("test_strcpy_empty: PASS\n");
}

void test_strncpy(void) {
    char src[] = "Hello";
    char dest[20];

    // Case 1: n > strlen(src) -> Pad with nulls
    memset(dest, 'X', sizeof(dest));
    char *ret = kernel_strncpy(dest, src, 10);

    ASSERT_EQ((uintptr_t)ret, (uintptr_t)dest, "strncpy return value");
    ASSERT_STREQ(dest, src, "strncpy content");
    // Verify padding
    for (int i = 5; i < 10; i++) {
        if (dest[i] != '\0') {
            printf("FAIL: strncpy padding at index %d is %02x, expected 00\n", i, (unsigned char)dest[i]);
            exit(1);
        }
    }
    ASSERT_EQ(dest[10], 'X', "strncpy overflow check");

    // Case 2: n < strlen(src) -> No null terminator
    memset(dest, 'X', sizeof(dest));
    kernel_strncpy(dest, src, 3);

    // Check first 3 chars match
    if (memcmp(dest, src, 3) != 0) {
        printf("FAIL: strncpy truncation content mismatch\n");
        exit(1);
    }
    // Check NO null terminator at index 3
    ASSERT_EQ(dest[3], 'X', "strncpy should not null terminate if truncated");

    // Case 3: n == strlen(src) -> No null terminator
    memset(dest, 'X', sizeof(dest));
    kernel_strncpy(dest, src, 5);

    if (memcmp(dest, src, 5) != 0) {
        printf("FAIL: strncpy exact length content mismatch\n");
        exit(1);
    }
    ASSERT_EQ(dest[5], 'X', "strncpy exact length should not null terminate");

    printf("test_strncpy: PASS\n");
}

<<<<<<< HEAD
void test_strncpy_empty(void) {
    char src[] = "";
    char dest[20];

    memset(dest, 'X', sizeof(dest));
    kernel_strncpy(dest, src, 5);

    // Verify padding (all 5 bytes should be null)
    for (int i = 0; i < 5; i++) {
        if (dest[i] != '\0') {
            printf("FAIL: strncpy empty padding at index %d\n", i);
            exit(1);
        }
    }
    ASSERT_EQ(dest[5], 'X', "strncpy empty overflow check");

    printf("test_strncpy_empty: PASS\n");
}

void test_strncpy_zero(void) {
    char src[] = "Hello";
    char dest[20];

    memset(dest, 'X', sizeof(dest));
    kernel_strncpy(dest, src, 0);

    // Verify nothing changed
    ASSERT_EQ(dest[0], 'X', "strncpy n=0 should not modify dest");

    printf("test_strncpy_zero: PASS\n");
=======
void test_strcmp(void) {
    ASSERT_EQ(kernel_strcmp("", ""), 0, "strcmp empty-empty");
    ASSERT_EQ(kernel_strcmp("a", "a"), 0, "strcmp equal single char");
    ASSERT_EQ(kernel_strcmp("abc", "abc"), 0, "strcmp equal string");

    // Check signs (implementation specific, but standard says <0, >0)
    // Our implementation returns difference of unsigned chars
    int res;

    res = kernel_strcmp("abc", "abd");
    if (res >= 0) {
        printf("FAIL: strcmp('abc', 'abd') >= 0 (got %d)\n", res);
        exit(1);
    }

    res = kernel_strcmp("abd", "abc");
    if (res <= 0) {
        printf("FAIL: strcmp('abd', 'abc') <= 0 (got %d)\n", res);
        exit(1);
    }

    res = kernel_strcmp("abc", "abcd");
    if (res >= 0) {
        printf("FAIL: strcmp('abc', 'abcd') >= 0 (got %d)\n", res);
        exit(1);
    }

    printf("test_strcmp: PASS\n");
}

void test_strncmp(void) {
    ASSERT_EQ(kernel_strncmp("", "", 0), 0, "strncmp empty-empty 0");
    ASSERT_EQ(kernel_strncmp("abc", "def", 0), 0, "strncmp different 0");
    ASSERT_EQ(kernel_strncmp("abc", "abc", 3), 0, "strncmp equal 3");
    ASSERT_EQ(kernel_strncmp("abc", "abc", 5), 0, "strncmp equal >len");

    int res;

    // Difference after n
    res = kernel_strncmp("abc", "abd", 2);
    ASSERT_EQ(res, 0, "strncmp equal prefix");

    // Difference within n
    res = kernel_strncmp("abc", "abd", 3);
    if (res >= 0) {
        printf("FAIL: strncmp('abc', 'abd', 3) >= 0 (got %d)\n", res);
        exit(1);
    }

    // Prefix
    res = kernel_strncmp("abc", "abcd", 3);
    ASSERT_EQ(res, 0, "strncmp prefix equal");

    res = kernel_strncmp("abc", "abcd", 4);
    if (res >= 0) {
        printf("FAIL: strncmp('abc', 'abcd', 4) >= 0 (got %d)\n", res);
        exit(1);
    }

    printf("test_strncmp: PASS\n");
>>>>>>> main
}

int main(void) {
    printf("Running String Tests (Host)\n");
    test_strcpy();
    test_strcpy_empty();
    test_strncpy();
<<<<<<< HEAD
    test_strncpy_empty();
    test_strncpy_zero();
=======
    test_strcmp();
    test_strncmp();
>>>>>>> main
    printf("All Tests Passed\n");
    return 0;
}
