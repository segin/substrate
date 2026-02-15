#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>

// Rename implemented functions to avoid conflicts with host libc
#define strfry libc_strfry
#define memcpy libc_memcpy
#define memmove libc_memmove
#define memset libc_memset
#define memcmp libc_memcmp
#define memchr libc_memchr
#define strcpy libc_strcpy
#define strncpy libc_strncpy
#define strcat libc_strcat
#define strncat libc_strncat
#define strcmp libc_strcmp
#define strncmp libc_strncmp
#define strchr libc_strchr
#define strrchr libc_strrchr
#define strstr libc_strstr
#define strlen libc_strlen
#define strdup libc_strdup
#define strspn libc_strspn
#define strcspn libc_strcspn
#define strtok libc_strtok
#define strpbrk libc_strpbrk
#define strtok_r libc_strtok_r
#define strerror libc_strerror
#define strcasecmp libc_strcasecmp
#define strncasecmp libc_strncasecmp

// Forward declarations for renamed functions
char *libc_strfry(char *string);
void *libc_memcpy(void *dest, const void *src, size_t n);
void *libc_memmove(void *dest, const void *src, size_t n);
void *libc_memset(void *s, int c, size_t n);
int libc_memcmp(const void *s1, const void *s2, size_t n);
void *libc_memchr(const void *s, int c, size_t n);
char *libc_strcpy(char *dest, const char *src);
char *libc_strncpy(char *dest, const char *src, size_t n);
char *libc_strcat(char *dest, const char *src);
char *libc_strncat(char *dest, const char *src, size_t n);
int libc_strcmp(const char *s1, const char *s2);
int libc_strncmp(const char *s1, const char *s2, size_t n);
char *libc_strchr(const char *s, int c);
char *libc_strrchr(const char *s, int c);
char *libc_strstr(const char *haystack, const char *needle);
size_t libc_strlen(const char *s);
char *libc_strdup(const char *s);
size_t libc_strspn(const char *s, const char *accept);
size_t libc_strcspn(const char *s, const char *reject);
char *libc_strtok(char *str, const char *delim);
char *libc_strpbrk(const char *s1, const char *s2);
char *libc_strtok_r(char *str, const char *delim, char **saveptr);
char *libc_strerror(int errnum);
int libc_strcasecmp(const char *s1, const char *s2);
int libc_strncasecmp(const char *s1, const char *s2, size_t n);

// Include the source file directly - Removed to avoid redefinitions
// The test now relies on string_prefixed.o linked by the Makefile

// libgcc/libc wrappers for prefixed builds
void* libc_malloc(size_t s) { return malloc(s); }
void libc_free(void* p) { free(p); }
int libc_rand(void) { return rand(); }

// Undef macros to restore original names if needed
#undef strfry
#undef memcpy
#undef memmove
#undef memset
#undef memcmp
#undef memchr
#undef strcpy
#undef strncpy
#undef strcat
#undef strncat
#undef strcmp
#undef strncmp
#undef strchr
#undef strrchr
#undef strstr
#undef strlen
#undef strdup
#undef strspn
#undef strcspn
#undef strtok
#undef strpbrk
#undef strtok_r
#undef strerror

// Helper macros for testing
#define ASSERT_EQ(actual, expected, msg) do { \
    if ((actual) != (expected)) { \
        fprintf(stderr, "FAIL: %s: expected %d, got %d\n", msg, (int)(expected), (int)(actual)); \
        exit(1); \
    } \
} while(0)

#define ASSERT_PTR_EQ(actual, expected, msg) do { \
    if ((actual) != (expected)) { \
        fprintf(stderr, "FAIL: %s: expected %p, got %p\n", msg, (void*)(expected), (void*)(actual)); \
        exit(1); \
    } \
} while(0)

#define ASSERT_TRUE(condition, msg) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL: %s\n", msg); \
        exit(1); \
    } \
} while(0)

#define ASSERT_MEM_EQ(a, b, size, msg) do { \
    if (memcmp(a, b, size) != 0) { \
        fprintf(stderr, "FAIL: %s\n", msg); \
        exit(1); \
    } \
} while(0)

void run_strcmp_tests(void) {
    printf("Running strcmp tests...\n");

    // Basic equality
    assert(libc_strcmp("abc", "abc") == 0);
    assert(libc_strcmp("", "") == 0);
    assert(libc_strcmp("hello world", "hello world") == 0);

    // Basic inequality (sign check)
    assert(libc_strcmp("abc", "abd") < 0);
    assert(libc_strcmp("abd", "abc") > 0);
    assert(libc_strcmp("abc", "ab") > 0);
    assert(libc_strcmp("ab", "abc") < 0);
    assert(libc_strcmp("", "a") < 0);
    assert(libc_strcmp("a", "") > 0);

    // High bit characters (unsigned char comparison)
    char s1[] = "\xff";
    char s2[] = "\x01";
    assert(libc_strcmp(s1, s2) > 0);
    assert(libc_strcmp(s2, s1) < 0);

    // Verify against host strcmp behavior (sign only)
    const char *h1 = "test string 1";
    const char *h2 = "test string 2";
    int res_test = libc_strcmp(h1, h2);
    int res_host = strcmp(h1, h2);

    if (res_host < 0) assert(res_test < 0);
    else if (res_host > 0) assert(res_test > 0);
    else assert(res_test == 0);

    printf("strcmp tests passed!\n");
}

void run_strncmp_tests(void) {
    printf("Running strncmp tests...\n");

    ASSERT_EQ(libc_strncmp("abc", "abc", 5), 0, "Equal strings, n > len");
    ASSERT_EQ(libc_strncmp("abc", "abc", 3), 0, "Equal strings, n == len");
    ASSERT_EQ(libc_strncmp("abc", "abc", 2), 0, "Equal strings, n < len");
    ASSERT_TRUE(libc_strncmp("abc", "bbc", 3) < 0, "abc < bbc");
    ASSERT_TRUE(libc_strncmp("bbc", "abc", 3) > 0, "bbc > abc");
    ASSERT_TRUE(libc_strncmp("abc", "abd", 3) < 0, "abc < abd");
    ASSERT_EQ(libc_strncmp("abc", "abd", 2), 0, "Difference after n ignored");
    ASSERT_TRUE(libc_strncmp("abc", "abcd", 4) < 0, "abc < abcd");
    ASSERT_TRUE(libc_strncmp("abcd", "abc", 4) > 0, "abcd > abc");
    ASSERT_EQ(libc_strncmp("abc", "abcd", 3), 0, "Prefix match within n");
    ASSERT_EQ(libc_strncmp("abc", "def", 0), 0, "n=0 should return 0");
    ASSERT_EQ(libc_strncmp("", "", 5), 0, "Empty strings equal");
    ASSERT_TRUE(libc_strncmp("", "a", 5) < 0, "Empty < a");
    ASSERT_TRUE(libc_strncmp("a", "", 5) > 0, "a > Empty");

    char s1[] = {'\200', 0};
    char s2[] = {'a', 0};
    ASSERT_TRUE(libc_strncmp(s1, s2, 1) > 0, "Unsigned char comparison");

    printf("strncmp tests passed!\n");
}

void run_strcasecmp_tests(void) {
    printf("Running strcasecmp tests...\n");

    ASSERT_EQ(libc_strcasecmp("abc", "abc"), 0, "abc == abc");
    ASSERT_EQ(libc_strcasecmp("abc", "ABC"), 0, "abc == ABC");
    ASSERT_EQ(libc_strcasecmp("ABC", "abc"), 0, "ABC == abc");
    ASSERT_EQ(libc_strcasecmp("", ""), 0, "Empty strings");
    ASSERT_TRUE(libc_strcasecmp("abc", "abd") < 0, "abc < abd");
    ASSERT_TRUE(libc_strcasecmp("abc", "ABD") < 0, "abc < ABD");
    ASSERT_TRUE(libc_strcasecmp("ABC", "abd") < 0, "ABC < abd");
    ASSERT_TRUE(libc_strcasecmp("abd", "abc") > 0, "abd > abc");
    ASSERT_TRUE(libc_strcasecmp("abc", "abcd") < 0, "abc < abcd");
    ASSERT_TRUE(libc_strcasecmp("abcd", "abc") > 0, "abcd > abc");

    printf("strcasecmp tests passed!\n");
}

void run_strncasecmp_tests(void) {
    printf("Running strncasecmp tests...\n");

    ASSERT_EQ(libc_strncasecmp("abc", "abc", 3), 0, "abc == abc (n=3)");
    ASSERT_EQ(libc_strncasecmp("abc", "ABC", 3), 0, "abc == ABC (n=3)");
    ASSERT_EQ(libc_strncasecmp("abc", "abd", 2), 0, "abc == abd (n=2)");
    ASSERT_EQ(libc_strncasecmp("abc", "def", 0), 0, "n=0");
    ASSERT_TRUE(libc_strncasecmp("abc", "abd", 3) < 0, "abc < abd (n=3)");
    ASSERT_EQ(libc_strncasecmp("abc", "abcd", 3), 0, "Prefix match within n");

    printf("strncasecmp tests passed!\n");
}

void run_strrchr_tests(void) {
    printf("Running strrchr tests...\n");
    const char *s = "hello world";
    const char *empty = "";

    assert(libc_strrchr(s, 'h') == s);
    assert(libc_strrchr(s, 'w') == s + 6);
    assert(libc_strrchr(s, 'd') == s + 10);
    assert(libc_strrchr(s, 'o') == s + 7);
    assert(libc_strrchr(s, 'l') == s + 9);
    assert(libc_strrchr(s, 'z') == NULL);
    assert(libc_strrchr(s, 'H') == NULL);
    assert(libc_strrchr(s, '\0') == s + 11);
    assert(libc_strrchr(empty, 'a') == NULL);
    assert(libc_strrchr(empty, '\0') == empty);
    assert(libc_strrchr(s, 'l') == strrchr(s, 'l'));
    assert(libc_strrchr(s, 0) == strrchr(s, 0));
    assert(libc_strrchr(empty, 0) == strrchr(empty, 0));

    printf("strrchr tests passed!\n");
}

void run_memcpy_tests(void) {
    printf("Running memcpy tests...\n");

    // Basic memcpy
    {
        char src[] = "Hello World";
        char dest[20] = {0};
        libc_memcpy(dest, src, 12);
        ASSERT_MEM_EQ(dest, src, 12, "Basic memcpy failed");
    }

    // Small memcpy consistency
    {
        char src[] = "12345678";
        char dest[10];
        for (int i = 0; i <= 8; i++) {
            memset(dest, 0, sizeof(dest));
            libc_memcpy(dest, src, i);
            ASSERT_MEM_EQ(dest, src, i, "Small memcpy consistency check");
        }
    }

    // Unaligned memcpy
    {
        char s_buf[64] __attribute__((aligned(16)));
        char d_buf[64] __attribute__((aligned(16)));
        for (int i = 0; i < 64; i++) s_buf[i] = (char)i;

        memset(d_buf, 0, 64);
        libc_memcpy(d_buf + 1, s_buf, 10);
        ASSERT_MEM_EQ(d_buf + 1, s_buf, 10, "Unaligned dest memcpy failed");

        memset(d_buf, 0, 64);
        libc_memcpy(d_buf, s_buf + 1, 10);
        ASSERT_MEM_EQ(d_buf, s_buf + 1, 10, "Unaligned src memcpy failed");

        memset(d_buf, 0, 64);
        libc_memcpy(d_buf + 1, s_buf + 1, 10);
        ASSERT_MEM_EQ(d_buf + 1, s_buf + 1, 10, "Both unaligned memcpy failed");
    }

    // Large memcpy
    {
        size_t size = 4096;
        char *src = malloc(size);
        char *dest = malloc(size);
        if (src && dest) {
            for (size_t i = 0; i < size; i++) src[i] = (char)(i & 0xFF);
            memset(dest, 0, size);
            libc_memcpy(dest, src, size);
            ASSERT_MEM_EQ(dest, src, size, "Large memcpy failed");
            free(src);
            free(dest);
        }
    }

    printf("memcpy tests passed!\n");
}

void run_memset_tests(void) {
    printf("Running memset tests...\n");

    // Basic memset
    {
        char buf[100];
        libc_memset(buf, 'A', 100);
        for (int i = 0; i < 100; i++) assert(buf[i] == 'A');
        libc_memset(buf, 0, 100);
        for (int i = 0; i < 100; i++) assert(buf[i] == 0);
    }

    // Alignment and size test
    {
        char *buf = malloc(128);
        if (buf) {
            for (int offset = 0; offset < 8; offset++) {
                for (size_t len = 0; len < 64; len++) {
                    for (int i = 0; i < 128; i++) buf[i] = 0;
                    int c = (offset + len) % 256;
                    libc_memset(buf + offset, c, len);
                    for (int i = 0; i < 128; i++) {
                        if (i >= offset && i < offset + (int)len) {
                            assert((unsigned char)buf[i] == (unsigned char)c);
                        } else {
                            assert(buf[i] == 0);
                        }
                    }
                }
            }
            free(buf);
        }
    }

    // Large memset
    {
        size_t size = 1024 * 1024;
        char *buf = malloc(size);
        if (buf) {
            libc_memset(buf, 0x55, size);
            for (size_t i = 0; i < size; i++) assert((unsigned char)buf[i] == 0x55);
            free(buf);
        }
    }

    printf("memset tests passed!\n");
}

void run_strncpy_tests(void) {
    printf("Running strncpy tests...\n");
    char src[] = "hello";
    char dest[10];

    // 1. n > src length (should pad with nulls)
    memset(dest, 'X', sizeof(dest));
    libc_strncpy(dest, src, 8);
    assert(strcmp(dest, "hello") == 0);
    assert(dest[5] == '\0');
    assert(dest[6] == '\0');
    assert(dest[7] == '\0');
    assert(dest[8] == 'X');

    // 2. n == src length (no null termination if exactly fits)
    memset(dest, 'X', sizeof(dest));
    libc_strncpy(dest, src, 5);
    assert(memcmp(dest, "hello", 5) == 0);
    assert(dest[5] == 'X');

    // 3. n < src length (truncation)
    memset(dest, 'X', sizeof(dest));
    libc_strncpy(dest, src, 3);
    assert(memcmp(dest, "hel", 3) == 0);
    assert(dest[3] == 'X');

    // 4. n = 0
    memset(dest, 'X', sizeof(dest));
    libc_strncpy(dest, src, 0);
    assert(dest[0] == 'X');

    // 5. empty src
    char empty[] = "";
    memset(dest, 'X', sizeof(dest));
    libc_strncpy(dest, empty, 3);
    assert(dest[0] == '\0');
    assert(dest[1] == '\0');
    assert(dest[2] == '\0');
    assert(dest[3] == 'X');

    printf("strncpy tests passed!\n");
}

void run_strlen_tests(void) {
    printf("Running strlen tests...\n");
    // Empty string
    ASSERT_EQ(libc_strlen(""), 0, "Empty string");
    // Single character
    ASSERT_EQ(libc_strlen("a"), 1, "Single character");
    // Regular string
    ASSERT_EQ(libc_strlen("abc"), 3, "abc length");
    ASSERT_EQ(libc_strlen("hello world"), 11, "hello world length");

    // String with embedded null (should stop at first null)
    char buf[] = {'h', 'i', '\0', 'x'};
    ASSERT_EQ(libc_strlen(buf), 2, "Embedded null");

    // Longer string
    char long_str[1025];
    for(int i = 0; i < 1024; i++) long_str[i] = 'A';
    long_str[1024] = '\0';
    ASSERT_EQ(libc_strlen(long_str), 1024, "Longer string");
    printf("strlen tests passed!\n");
}

void run_memmove_tests(void) {
    printf("Running memmove tests...\n");
    char buf[256];

    // Case 1: Non-overlapping copy (forward)
    strcpy(buf, "Hello World");
    memset(buf + 20, 0, 20);
    libc_memmove(buf + 20, buf, 12);
    ASSERT_TRUE(strcmp(buf + 20, "Hello World") == 0, "Non-overlapping forward");

    // Case 2: Overlapping copy (backward) - dest > src
    strcpy(buf, "0123456789");
    libc_memmove(buf + 2, buf, 4);
    ASSERT_TRUE(strncmp(buf, "0101236789", 10) == 0, "Overlapping backward");

    // Case 3: Overlapping copy (forward) - dest < src
    strcpy(buf, "0123456789");
    libc_memmove(buf, buf + 2, 4);
    ASSERT_TRUE(strncmp(buf, "2345456789", 10) == 0, "Overlapping forward");

    // Case 4: Exact overlap (dest == src)
    strcpy(buf, "abcdef");
    libc_memmove(buf, buf, 6);
    ASSERT_TRUE(strcmp(buf, "abcdef") == 0, "Exact overlap");

    // Case 5: Zero length
    strcpy(buf, "abcdef");
    libc_memmove(buf + 1, buf, 0);
    ASSERT_TRUE(strcmp(buf, "abcdef") == 0, "Zero length");

    // Case 6: Unaligned access / boundaries
    for(int i = 0; i < 64; i++) buf[i] = (char)i;
    libc_memmove(buf + 4, buf + 1, 7);
    for(int i = 0; i < 7; i++) {
        ASSERT_EQ(buf[4+i], (char)(i+1), "Unaligned boundary");
    }
    ASSERT_EQ(buf[3], 3, "Surrounding byte check (before)");
    ASSERT_EQ(buf[11], 11, "Surrounding byte check (after)");

    printf("memmove tests passed!\n");
}

bool test_libc_strlen(void) {
    run_strlen_tests();
    return true;
}

bool test_libc_memmove(void) {
    run_memmove_tests();
    return true;
}

void run_memchr_tests(void) {
    printf("Running memchr tests...\n");

    const char *str = "Hello World";

    // 1. Basic search
    ASSERT_PTR_EQ(libc_memchr(str, 'H', 11), (void *)str, "Found at beginning");
    ASSERT_PTR_EQ(libc_memchr(str, 'W', 11), (void *)(str + 6), "Found in middle");
    ASSERT_PTR_EQ(libc_memchr(str, 'd', 11), (void *)(str + 10), "Found at end");

    // 2. Character not found
    ASSERT_PTR_EQ(libc_memchr(str, 'z', 11), NULL, "Not found");

    // 3. Search limit (n)
    ASSERT_PTR_EQ(libc_memchr(str, 'W', 5), NULL, "Exists but beyond limit");
    ASSERT_PTR_EQ(libc_memchr(str, 'o', 5), (void *)(str + 4), "Found at limit boundary");

    // 4. Empty buffer (n=0)
    ASSERT_PTR_EQ(libc_memchr(str, 'H', 0), NULL, "n=0");

    // 5. Binary data / high bit
    char bin[] = {0x01, (char)0xFF, 0x00, (char)0x80};
    ASSERT_PTR_EQ(libc_memchr(bin, 0xFF, 4), (void *)(bin + 1), "High bit character");
    ASSERT_PTR_EQ(libc_memchr(bin, 0x00, 4), (void *)(bin + 2), "Null byte in binary");

    // 6. Multiple occurrences (should return first)
    const char *dup = "banana";
    ASSERT_PTR_EQ(libc_memchr(dup, 'a', 6), (void *)(dup + 1), "First occurrence");

    // 7. Large value for c (should only consider unsigned char)
    ASSERT_PTR_EQ(libc_memchr(bin, 0xFF + 256, 4), (void *)(bin + 1), "Value > 255");

    printf("memchr tests passed!\n");
}

#ifndef NO_MAIN
int main(void) {
    run_strcmp_tests();
    run_strncmp_tests();
    run_strcasecmp_tests();
    run_strncasecmp_tests();
    run_strrchr_tests();
    run_memcpy_tests();
    run_memset_tests();
    run_strncpy_tests();
    run_strlen_tests();
    run_memmove_tests();
    run_memchr_tests();
    return 0;
}
#endif
