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

// Undefine to use libc versions for verification
#undef memcpy
#undef memset
#undef memmove
#undef memcmp
#undef strlen
#undef strcpy
#undef strncpy
#undef strcmp
#undef strchr
#undef strspn
#undef strpbrk

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

void test_memmove(void) {
    char buf[32];
    char expected[32];

    // Basic non-overlapping
    memset(buf, 0, sizeof(buf));
    strcpy(buf, "Hello World");
    kernel_memmove(buf + 20, buf, 12);
    ASSERT_MEM_EQ(buf + 20, "Hello World", 12, "memmove basic");

    // Overlap forward (dest < src)
    strcpy(buf, "12345678");
    strcpy(expected, "23456678");
    kernel_memmove(buf, buf + 1, 5);
    ASSERT_MEM_EQ(buf, expected, 8, "memmove overlap forward (dest < src)");

    // Overlap backward (dest > src)
    strcpy(buf, "12345678");
    strcpy(expected, "11234578");
    kernel_memmove(buf + 1, buf, 5);
    ASSERT_MEM_EQ(buf, expected, 8, "memmove overlap backward (dest > src)");

    // Exact overlap
    strcpy(buf, "12345678");
    kernel_memmove(buf, buf, 8);
    ASSERT_MEM_EQ(buf, "12345678", 8, "memmove exact overlap");

    // Zero size
    strcpy(buf, "12345678");
    kernel_memmove(buf, buf + 1, 0);
    ASSERT_MEM_EQ(buf, "12345678", 8, "memmove zero size");

    printf("test_memmove: PASS\n");
}

void test_memmove_comprehensive(void) {
    const int buffer_size = 256;
    char *buffer = malloc(buffer_size);
    char *control = malloc(buffer_size);

    if (!buffer || !control) {
        printf("SKIP: test_memmove_comprehensive (OOM)\n");
        exit(1);
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
                 // Reset buffer content
                 for (int i = 0; i < buffer_size; i++) {
                     buffer[i] = (char)(i & 0xFF);
                     control[i] = (char)(i & 0xFF);
                 }

                 if (src_off + len > buffer_size || dst_off + len > buffer_size) continue;

                 // Control (libc memmove)
                 memmove(control + dst_off, control + src_off, len);

                 // Test (kernel memmove)
                 kernel_memmove(buffer + dst_off, buffer + src_off, len);

                 // Compare
                 if (memcmp(buffer, control, buffer_size) != 0) {
                     printf("FAIL: memmove comprehensive mismatch at src=%d dst=%d len=%d\n", src_off, dst_off, len);
                     exit(1);
                 }
            }
        }
    }

    free(buffer);
    free(control);
    printf("test_memmove_comprehensive: PASS\n");
}

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
}

void test_strspn(void) {
    // Basic functionality
    ASSERT_EQ(kernel_strspn("hello", "he"), 2, "strspn basic prefix");
    ASSERT_EQ(kernel_strspn("hello", "l"), 0, "strspn no match at start");
    ASSERT_EQ(kernel_strspn("hello", "hel"), 4, "strspn longer prefix");
    ASSERT_EQ(kernel_strspn("hello", "oleh"), 5, "strspn full string match (scrambled set)");
    ASSERT_EQ(kernel_strspn("", "anything"), 0, "strspn empty string");
    ASSERT_EQ(kernel_strspn("hello", ""), 0, "strspn empty accept");

    // Comprehensive check against libc strspn
    const char *test_strings[] = {
        "hello world",
        "1234567890",
        "abcdef",
        "",
        "   leading spaces",
        "trailing spaces   ",
        "!@#$%^&*()",
        NULL
    };

    const char *accept_sets[] = {
        "helo",
        "123",
        " ",
        "abc",
        "xyz",
        "",
        "!@#",
        NULL
    };

    for (int i = 0; test_strings[i]; i++) {
        for (int j = 0; accept_sets[j]; j++) {
            size_t k_res = kernel_strspn(test_strings[i], accept_sets[j]);
            size_t l_res = strspn(test_strings[i], accept_sets[j]);

            if (k_res != l_res) {
                 printf("FAIL: strspn mismatch for s='%s', accept='%s'. Kernel: %zu, Libc: %zu\n",
                        test_strings[i], accept_sets[j], k_res, l_res);
                 exit(1);
            }
        }
    }

    printf("test_strspn: PASS\n");
}

void test_strchr(void) {
    char buf[] = "Hello World";

    // Found at beginning
    ASSERT_EQ((uintptr_t)kernel_strchr(buf, 'H'), (uintptr_t)buf, "strchr 'H' (beginning)");

    // Found in middle
    ASSERT_EQ((uintptr_t)kernel_strchr(buf, 'W'), (uintptr_t)(buf + 6), "strchr 'W' (middle)");

    // Found at end
    ASSERT_EQ((uintptr_t)kernel_strchr(buf, 'd'), (uintptr_t)(buf + 10), "strchr 'd' (end)");

    // Not found
    ASSERT_EQ((uintptr_t)kernel_strchr(buf, 'z'), (uintptr_t)NULL, "strchr 'z' (not found)");

    // Null terminator search
    ASSERT_EQ((uintptr_t)kernel_strchr(buf, '\0'), (uintptr_t)(buf + 11), "strchr '\\0' (terminator)");

    // Empty string
    char empty[] = "";
    ASSERT_EQ((uintptr_t)kernel_strchr(empty, '\0'), (uintptr_t)empty, "strchr empty string '\\0'");
    ASSERT_EQ((uintptr_t)kernel_strchr(empty, 'a'), (uintptr_t)NULL, "strchr empty string 'a'");

    // Multiple occurrences
    char repeated[] = "bananana";
    ASSERT_EQ((uintptr_t)kernel_strchr(repeated, 'a'), (uintptr_t)(repeated + 1), "strchr multiple 'a' (first occurrence)");

    printf("test_strchr: PASS\n");
}

int main(void) {
    printf("Running String Tests (Host)\n");
    test_strcpy();
    test_strncpy();
    test_memmove();
    test_memmove_comprehensive();
    test_strcmp();
    test_strncmp();
    test_strspn();
    test_strchr();
    printf("All Tests Passed\n");
    return 0;
}
