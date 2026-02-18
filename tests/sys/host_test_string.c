#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

// Mock kernel library functions for host testing
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

    ASSERT_EQ(kernel_memcmp(b1, b2, 0), 0, "memcmp n=0");

    printf("test_memcmp: PASS\n");
}

void test_memcmp_comprehensive(void) {
    const int buffer_size = 256;
    unsigned char *b1 = malloc(buffer_size);
    unsigned char *b2 = malloc(buffer_size);

    // Test 1: Identical buffers
    for (int i = 0; i < buffer_size; i++) {
        b1[i] = (unsigned char)i;
        b2[i] = (unsigned char)i;
    }
    ASSERT_EQ(kernel_memcmp(b1, b2, buffer_size), 0, "memcmp comprehensive identical");
    ASSERT_EQ(kernel_memcmp(b1, b2, 0), 0, "memcmp comprehensive n=0");

    // Test 2: Single byte differences
    for (int i = 0; i < buffer_size; i++) {
        if (b1[i] < 255) {
            b2[i] = b1[i] + 1;
            // kernel_memcmp should return negative since b1[i] < b2[i]
            if (kernel_memcmp(b1, b2, buffer_size) >= 0) {
                printf("FAIL: memcmp comprehensive < mismatch at index %d\n", i);
                exit(1);
            }
            b2[i] = b1[i]; // Restore
        }

        if (b1[i] > 0) {
            b2[i] = b1[i] - 1;
            // kernel_memcmp should return positive since b1[i] > b2[i]
            if (kernel_memcmp(b1, b2, buffer_size) <= 0) {
                printf("FAIL: memcmp comprehensive > mismatch at index %d\n", i);
                exit(1);
            }
            b2[i] = b1[i]; // Restore
        }
    }

    // Test 3: Unsigned comparison logic check
    // b1 has 0x00, b2 has 0xFF. b1 < b2.
    memset(b1, 0, buffer_size);
    memset(b2, 0, buffer_size);
    b1[0] = 0x00;
    b2[0] = 0xFF;
    if (kernel_memcmp(b1, b2, 1) >= 0) {
        printf("FAIL: memcmp unsigned comparison (0x00 vs 0xFF)\n");
        exit(1);
    }

    // b1 has 0x7F, b2 has 0x80. b1 < b2.
    b1[0] = 0x7F;
    b2[0] = 0x80;
    if (kernel_memcmp(b1, b2, 1) >= 0) {
        printf("FAIL: memcmp unsigned comparison (0x7F vs 0x80)\n");
        exit(1);
    }

    free(b1);
    free(b2);
    printf("test_memcmp_comprehensive: PASS\n");
}

void test_strcmp(void) {
    ASSERT_EQ(kernel_strcmp("", ""), 0, "strcmp empty");
    ASSERT_EQ(kernel_strcmp("abc", "abc"), 0, "strcmp equal");
    if (kernel_strcmp("abc", "abd") >= 0) exit(1);
    if (kernel_strcmp("abd", "abc") <= 0) exit(1);
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

void test_strpbrk(void) {
    const char *s = "hello world";

    // 1. Basic match: 'e' is in "abcde"
    ASSERT_EQ((uintptr_t)kernel_strpbrk(s, "abcde"), (uintptr_t)(s + 1), "strpbrk basic match 'e'");

    // 2. Basic match: 'o' is in "wor"
    ASSERT_EQ((uintptr_t)kernel_strpbrk(s, "wor"), (uintptr_t)(s + 4), "strpbrk basic match 'o'");

    // 3. No match
    ASSERT_EQ((uintptr_t)kernel_strpbrk(s, "xyz"), 0, "strpbrk no match");

    // 4. Multiple matches (first one in string s returned)
    // "hello world", accept "lo" -> first 'l' at index 2
    ASSERT_EQ((uintptr_t)kernel_strpbrk(s, "lo"), (uintptr_t)(s + 2), "strpbrk multiple matches");

    // 5. Match at beginning
    ASSERT_EQ((uintptr_t)kernel_strpbrk(s, "h"), (uintptr_t)s, "strpbrk match start");

    // 6. Match at end
    ASSERT_EQ((uintptr_t)kernel_strpbrk(s, "d"), (uintptr_t)(s + 10), "strpbrk match end");

    // 7. Empty source string
    ASSERT_EQ((uintptr_t)kernel_strpbrk("", "abc"), 0, "strpbrk empty source");

    // 8. Empty accept string
    ASSERT_EQ((uintptr_t)kernel_strpbrk(s, ""), 0, "strpbrk empty accept");

    // 9. Accept string with characters not in source
    ASSERT_EQ((uintptr_t)kernel_strpbrk("abc", "z"), 0, "strpbrk not in source");

    // 10. Accept string is substring of source (but chars are set)
    // "hello", accept "el" -> first match 'e' at index 1
    const char *s_subset = "hello";
    ASSERT_EQ((uintptr_t)kernel_strpbrk(s_subset, "el"), (uintptr_t)(s_subset + 1), "strpbrk accept subset");

    // 11. Source contains duplicates, accept matches one
    // "banana", accept "n" -> first 'n' at index 2
    const char *s_banana = "banana";
    ASSERT_EQ((uintptr_t)kernel_strpbrk(s_banana, "n"), (uintptr_t)(s_banana + 2), "strpbrk source dups");

    // 12. Accept contains duplicates (should not matter)
    // "hello", accept "ll" -> matches first 'l' at index 2
    const char *s_hello = "hello";
    ASSERT_EQ((uintptr_t)kernel_strpbrk(s_hello, "ll"), (uintptr_t)(s_hello + 2), "strpbrk accept dups");

    // 13. Long string test
    char long_str[100];
    memset(long_str, 'a', 99);
    long_str[99] = '\0';
    long_str[50] = 'b';
    ASSERT_EQ((uintptr_t)kernel_strpbrk(long_str, "b"), (uintptr_t)(long_str + 50), "strpbrk long string");

    // 14. Verify against host implementation
    {
        const char *t1 = "The quick brown fox jumps over the lazy dog";
        const char *accept = "aeiou";
        ASSERT_EQ((uintptr_t)kernel_strpbrk(t1, accept), (uintptr_t)strpbrk(t1, accept), "strpbrk host match 1");

        const char *t2 = "Pythons are amazing";
        const char *accept2 = "z";
        ASSERT_EQ((uintptr_t)kernel_strpbrk(t2, accept2), (uintptr_t)strpbrk(t2, accept2), "strpbrk host match 2");
    }

    printf("test_strpbrk: PASS\n");
}

int main(void) {
    printf("Running String Tests (Host)\n");
    test_strcpy();
    test_strncpy();
    test_memmove_comprehensive();
    test_memcmp();
    test_memcmp_comprehensive();
    test_strcmp();
    test_strncmp();
    test_strspn();
    test_strchr();
    test_strpbrk();
    printf("All Tests Passed\n");
    return 0;
}
