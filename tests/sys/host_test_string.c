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
    ASSERT_EQ(dest[sizeof(dest)-1], 'X', "strcpy buffer overflow check"); // Check we didn't write past end (though this is risky if src is too long, here it's fine)

    printf("test_strcpy: PASS\n");
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

    // Case 3: n == strlen(src) -> No null terminator (standard behavior? let's check man page)
    // man strncpy: "Warning: If there is no null byte among the first n bytes of src, the string placed in dest will not be null-terminated."
    memset(dest, 'X', sizeof(dest));
    kernel_strncpy(dest, src, 5);

    if (memcmp(dest, src, 5) != 0) {
        printf("FAIL: strncpy exact length content mismatch\n");
        exit(1);
    }
    ASSERT_EQ(dest[5], 'X', "strncpy exact length should not null terminate");

    printf("test_strncpy: PASS\n");
}

void test_strcpy_edge_cases(void) {
    char dest[20];

    // Empty string
    memset(dest, 'X', sizeof(dest));
    kernel_strcpy(dest, "");
    ASSERT_STREQ(dest, "", "strcpy empty string");
    ASSERT_EQ(dest[1], 'X', "strcpy empty string overflow check");

    // Single character
    memset(dest, 'X', sizeof(dest));
    kernel_strcpy(dest, "A");
    ASSERT_STREQ(dest, "A", "strcpy single char");
    ASSERT_EQ(dest[2], 'X', "strcpy single char overflow check");

    printf("test_strcpy_edge_cases: PASS\n");
}

void test_strncpy_edge_cases(void) {
    char dest[20];
    char src[] = "Hello";

    // n = 0
    memset(dest, 'X', sizeof(dest));
    kernel_strncpy(dest, src, 0);
    ASSERT_EQ(dest[0], 'X', "strncpy n=0 should not write");

    // n = 1, src empty
    memset(dest, 'X', sizeof(dest));
    kernel_strncpy(dest, "", 1);
    ASSERT_EQ(dest[0], '\0', "strncpy empty src n=1");
    ASSERT_EQ(dest[1], 'X', "strncpy empty src n=1 overflow");

    // n = 1, src not empty
    memset(dest, 'X', sizeof(dest));
    kernel_strncpy(dest, "A", 1);
    ASSERT_EQ(dest[0], 'A', "strncpy n=1 src='A'");
    ASSERT_EQ(dest[1], 'X', "strncpy n=1 src='A' overflow"); // Should NOT null terminate if len >= n

    // src empty, n large
    memset(dest, 'X', sizeof(dest));
    kernel_strncpy(dest, "", 5);
    for(int i=0; i<5; i++) {
        if (dest[i] != '\0') {
            printf("FAIL: strncpy empty src large n padding at %d\n", i);
            exit(1);
        }
    }
    ASSERT_EQ(dest[5], 'X', "strncpy empty src large n overflow");

    printf("test_strncpy_edge_cases: PASS\n");
}

int main(void) {
    printf("Running String Tests (Host)\n");
    test_strcpy();
    test_strcpy_edge_cases();
    test_strncpy();
    test_strncpy_edge_cases();
    printf("All Tests Passed\n");
    return 0;
}
