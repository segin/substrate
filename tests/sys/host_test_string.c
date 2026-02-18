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
    ASSERT_EQ(dest[sizeof(dest)-1], 'X', "strcpy buffer overflow check");

    printf("test_strcpy: PASS\n");
}

void test_strncpy(void) {
    char src[] = "Hello";
    char dest[20];

    memset(dest, 'X', sizeof(dest));
    char *ret = kernel_strncpy(dest, src, 10);

    ASSERT_EQ((uintptr_t)ret, (uintptr_t)dest, "strncpy return value");
    ASSERT_STREQ(dest, src, "strncpy content");
    for (int i = 5; i < 10; i++) {
        if (dest[i] != '\0') {
            printf("FAIL: strncpy padding at index %d is %02x, expected 00\n", i, (unsigned char)dest[i]);
            exit(1);
        }
    }
    ASSERT_EQ(dest[10], 'X', "strncpy overflow check");

    memset(dest, 'X', sizeof(dest));
    kernel_strncpy(dest, src, 3);
    if (memcmp(dest, src, 3) != 0) {
        printf("FAIL: strncpy truncation content mismatch\n");
        exit(1);
    }
    ASSERT_EQ(dest[3], 'X', "strncpy should not null terminate if truncated");

    memset(dest, 'X', sizeof(dest));
    kernel_strncpy(dest, src, 5);
    if (memcmp(dest, src, 5) != 0) {
        printf("FAIL: strncpy exact length content mismatch\n");
        exit(1);
    }
    ASSERT_EQ(dest[5], 'X', "strncpy exact length should not null terminate");

    printf("test_strncpy: PASS\n");
}

void test_memmove_comprehensive(void) {
    const int buffer_size = 256;
    char *buffer = malloc(buffer_size);
    char *control = malloc(buffer_size);

    for (int src_off = 0; src_off < buffer_size - 16; src_off += 13) {
        for (int dst_off = 0; dst_off < buffer_size - 16; dst_off += 17) {
            for (int len = 0; len < 64; len++) {
                 for (int i = 0; i < buffer_size; i++) {
                     buffer[i] = (char)(i & 0xFF);
                     control[i] = (char)(i & 0xFF);
                 }
                 if (src_off + len > buffer_size || dst_off + len > buffer_size) continue;
                 memmove(control + dst_off, control + src_off, len);
                 kernel_memmove(buffer + dst_off, buffer + src_off, len);
                 if (memcmp(buffer, control, buffer_size) != 0) {
                     printf("FAIL: memmove comprehensive mismatch\n");
                     exit(1);
                 }
            }
        }
    }
    free(buffer);
    free(control);
    printf("test_memmove_comprehensive: PASS\n");
}

void test_memcmp(void) {
    char b1[20], b2[20];
    memset(b1, 0, sizeof(b1));
    memset(b2, 0, sizeof(b2));
    ASSERT_EQ(kernel_memcmp(b1, b2, sizeof(b1)), 0, "memcmp identity zero");

    strcpy(b1, "Hello");
    strcpy(b2, "Hello");
    ASSERT_EQ(kernel_memcmp(b1, b2, 6), 0, "memcmp identity string");

    b2[0] = 'h';
    if (kernel_memcmp(b1, b2, 1) >= 0) exit(1);
    
    b1[0] = 'b'; b2[0] = 'a';
    if (kernel_memcmp(b1, b2, 1) <= 0) exit(1);

    unsigned char u1[] = { 0xFF }, u2[] = { 0x00 };
    if (kernel_memcmp(u1, u2, 1) <= 0) exit(1);

    printf("test_memcmp: PASS\n");
}

void test_strcmp(void) {
    // Basic equality
    ASSERT_EQ(kernel_strcmp("", ""), 0, "strcmp empty");
    ASSERT_EQ(kernel_strcmp("abc", "abc"), 0, "strcmp equal");

    // Basic inequality
    if (kernel_strcmp("abc", "abd") >= 0) {
        printf("FAIL: strcmp('abc', 'abd') should be negative\n");
        exit(1);
    }
    if (kernel_strcmp("abd", "abc") <= 0) {
        printf("FAIL: strcmp('abd', 'abc') should be positive\n");
        exit(1);
    }

    // Prefix handling
    if (kernel_strcmp("abc", "abcd") >= 0) {
        printf("FAIL: strcmp prefix ('abc', 'abcd') should be negative\n");
        exit(1);
    }
    if (kernel_strcmp("abcd", "abc") <= 0) {
        printf("FAIL: strcmp prefix ('abcd', 'abc') should be positive\n");
        exit(1);
    }

    // Empty vs Non-empty
    if (kernel_strcmp("", "a") >= 0) {
        printf("FAIL: strcmp empty vs 'a' should be negative\n");
        exit(1);
    }
    if (kernel_strcmp("a", "") <= 0) {
        printf("FAIL: strcmp 'a' vs empty should be positive\n");
        exit(1);
    }

    // Unsigned char comparison (High bit set)
    // '\xff' is 255 (unsigned), so it should be greater than '\x01' (1)
    if (kernel_strcmp("\xff", "\x01") <= 0) {
        printf("FAIL: strcmp unsigned comparison ('\\xff', '\\x01') should be positive\n");
        exit(1);
    }
    if (kernel_strcmp("\x01", "\xff") >= 0) {
        printf("FAIL: strcmp unsigned comparison ('\\x01', '\\xff') should be negative\n");
        exit(1);
    }

    printf("test_strcmp: PASS\n");
}

void test_strncmp(void) {
    ASSERT_EQ(kernel_strncmp("abc", "abd", 2), 0, "strncmp equal prefix");
    if (kernel_strncmp("abc", "abd", 3) >= 0) exit(1);
    printf("test_strncmp: PASS\n");
}

void test_strspn(void) {
    ASSERT_EQ(kernel_strspn("hello", "he"), 2, "strspn basic");
    ASSERT_EQ(kernel_strspn("hello", "oleh"), 5, "strspn full");
    ASSERT_EQ(kernel_strspn("hello", ""), 0, "strspn empty accept");
    printf("test_strspn: PASS\n");
}

void test_strchr(void) {
    char buf[] = "Hello World";
    ASSERT_EQ((uintptr_t)kernel_strchr(buf, 'W'), (uintptr_t)(buf + 6), "strchr found");
    ASSERT_EQ((uintptr_t)kernel_strchr(buf, 'z'), (uintptr_t)NULL, "strchr not found");
    ASSERT_EQ((uintptr_t)kernel_strchr(buf, '\0'), (uintptr_t)(buf + 11), "strchr null");
    printf("test_strchr: PASS\n");
}

void test_strchr_comprehensive(void) {
    char buf[] = "Hello World";

    // Test: Search for a character not present in the string
    ASSERT_EQ((uintptr_t)kernel_strchr(buf, 'z'), (uintptr_t)NULL, "strchr comprehensive not found");

    // Test: Search for the null terminator
    ASSERT_EQ((uintptr_t)kernel_strchr(buf, '\0'), (uintptr_t)(buf + 11), "strchr comprehensive null terminator");

    // Test: Search in an empty string
    char empty[] = "";
    ASSERT_EQ((uintptr_t)kernel_strchr(empty, 'a'), (uintptr_t)NULL, "strchr comprehensive empty string not found");
    ASSERT_EQ((uintptr_t)kernel_strchr(empty, '\0'), (uintptr_t)empty, "strchr comprehensive empty string null terminator");

    // Test: Verify int c argument conversion (e.g., c values > 255)
    // 'W' is 87. 87 + 256 = 343. (char)343 is 87 ('W').
    ASSERT_EQ((uintptr_t)kernel_strchr(buf, 'W' + 256), (uintptr_t)(buf + 6), "strchr comprehensive int conversion > 255");

    // Test: Search for high-bit characters (0x80-0xFF)
    unsigned char high_bit_buf[] = { 0x80, 0xFF, 0x00 };
    ASSERT_EQ((uintptr_t)kernel_strchr((char *)high_bit_buf, 0x80), (uintptr_t)high_bit_buf, "strchr comprehensive high bit 0x80");
    ASSERT_EQ((uintptr_t)kernel_strchr((char *)high_bit_buf, 0xFF), (uintptr_t)(high_bit_buf + 1), "strchr comprehensive high bit 0xFF");

    // Test: Verify function returns the first occurrence
    char multiple[] = "ababa";
    ASSERT_EQ((uintptr_t)kernel_strchr(multiple, 'a'), (uintptr_t)multiple, "strchr comprehensive first occurrence 'a'");
    ASSERT_EQ((uintptr_t)kernel_strchr(multiple, 'b'), (uintptr_t)(multiple + 1), "strchr comprehensive first occurrence 'b'");

    printf("test_strchr_comprehensive: PASS\n");
}

void test_strpbrk(void) {
    const char *s = "hello world";
    ASSERT_EQ((uintptr_t)kernel_strpbrk(s, "abcde"), (uintptr_t)(s + 1), "strpbrk 'e'");
    ASSERT_EQ((uintptr_t)kernel_strpbrk(s, "xyz"), 0, "strpbrk no match");
    ASSERT_EQ((uintptr_t)kernel_strpbrk(s, "lo"), (uintptr_t)(s + 2), "strpbrk multiple");
    printf("test_strpbrk: PASS\n");
}

int main(void) {
    printf("Running String Tests (Host)\n");
    test_strcpy();
    test_strncpy();
    test_memmove_comprehensive();
    test_memcmp();
    test_strcmp();
    test_strncmp();
    test_strspn();
    test_strchr();
    test_strchr_comprehensive();
    test_strpbrk();
    printf("All Tests Passed\n");
    return 0;
}
