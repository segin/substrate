#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>

// Rename implemented functions to avoid conflicts with host libc
#define strfry test_strfry
#define memcpy test_memcpy
#define memmove test_memmove
#define memset test_memset
#define memcmp test_memcmp
#define memchr test_memchr
#define strcpy test_strcpy
#define strncpy test_strncpy
#define strcat test_strcat
#define strncat test_strncat
#define strcmp test_strcmp
#define strncmp test_strncmp
#define strchr test_strchr
#define strrchr test_strrchr
#define strstr test_strstr
#define strlen test_strlen
#define strdup test_strdup
#define strspn test_strspn
#define strcspn test_strcspn
#define strtok test_strtok
#define strpbrk test_strpbrk
#define strtok_r test_strtok_r
#define strerror test_strerror

// Forward declarations for renamed functions
char *test_strfry(char *string);
void *test_memcpy(void *dest, const void *src, size_t n);
void *test_memmove(void *dest, const void *src, size_t n);
void *test_memset(void *s, int c, size_t n);
int test_memcmp(const void *s1, const void *s2, size_t n);
void *test_memchr(const void *s, int c, size_t n);
char *test_strcpy(char *dest, const char *src);
char *test_strncpy(char *dest, const char *src, size_t n);
char *test_strcat(char *dest, const char *src);
char *test_strncat(char *dest, const char *src, size_t n);
int test_strcmp(const char *s1, const char *s2);
int test_strncmp(const char *s1, const char *s2, size_t n);
char *test_strchr(const char *s, int c);
char *test_strrchr(const char *s, int c);
char *test_strstr(const char *haystack, const char *needle);
size_t test_strlen(const char *s);
char *test_strdup(const char *s);
size_t test_strspn(const char *s, const char *accept);
size_t test_strcspn(const char *s, const char *reject);
char *test_strtok(char *str, const char *delim);
char *test_strpbrk(const char *s1, const char *s2);
char *test_strtok_r(char *str, const char *delim, char **saveptr);
char *test_strerror(int errnum);

// Include the source file directly
#include "../../../lib/c/src/string.c"

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
    assert(test_strcmp("abc", "abc") == 0);
    assert(test_strcmp("", "") == 0);
    assert(test_strcmp("hello world", "hello world") == 0);

    // Basic inequality (sign check)
    assert(test_strcmp("abc", "abd") < 0);
    assert(test_strcmp("abd", "abc") > 0);
    assert(test_strcmp("abc", "ab") > 0);
    assert(test_strcmp("ab", "abc") < 0);
    assert(test_strcmp("", "a") < 0);
    assert(test_strcmp("a", "") > 0);

    // High bit characters (unsigned char comparison)
    char s1[] = "\xff";
    char s2[] = "\x01";
    assert(test_strcmp(s1, s2) > 0);
    assert(test_strcmp(s2, s1) < 0);

    // Verify against host strcmp behavior (sign only)
    const char *h1 = "test string 1";
    const char *h2 = "test string 2";
    int res_test = test_strcmp(h1, h2);
    int res_host = strcmp(h1, h2);

    if (res_host < 0) assert(res_test < 0);
    else if (res_host > 0) assert(res_test > 0);
    else assert(res_test == 0);

    printf("strcmp tests passed!\n");
}

void run_strncmp_tests(void) {
    printf("Running strncmp tests...\n");

    ASSERT_EQ(test_strncmp("abc", "abc", 5), 0, "Equal strings, n > len");
    ASSERT_EQ(test_strncmp("abc", "abc", 3), 0, "Equal strings, n == len");
    ASSERT_EQ(test_strncmp("abc", "abc", 2), 0, "Equal strings, n < len");
    ASSERT_TRUE(test_strncmp("abc", "bbc", 3) < 0, "abc < bbc");
    ASSERT_TRUE(test_strncmp("bbc", "abc", 3) > 0, "bbc > abc");
    ASSERT_TRUE(test_strncmp("abc", "abd", 3) < 0, "abc < abd");
    ASSERT_EQ(test_strncmp("abc", "abd", 2), 0, "Difference after n ignored");
    ASSERT_TRUE(test_strncmp("abc", "abcd", 4) < 0, "abc < abcd");
    ASSERT_TRUE(test_strncmp("abcd", "abc", 4) > 0, "abcd > abc");
    ASSERT_EQ(test_strncmp("abc", "abcd", 3), 0, "Prefix match within n");
    ASSERT_EQ(test_strncmp("abc", "def", 0), 0, "n=0 should return 0");
    ASSERT_EQ(test_strncmp("", "", 5), 0, "Empty strings equal");
    ASSERT_TRUE(test_strncmp("", "a", 5) < 0, "Empty < a");
    ASSERT_TRUE(test_strncmp("a", "", 5) > 0, "a > Empty");

    char s1[] = {'\200', 0};
    char s2[] = {'a', 0};
    ASSERT_TRUE(test_strncmp(s1, s2, 1) > 0, "Unsigned char comparison");

    printf("strncmp tests passed!\n");
}

void run_strrchr_tests(void) {
    printf("Running strrchr tests...\n");
    const char *s = "hello world";
    const char *empty = "";

    assert(test_strrchr(s, 'h') == s);
    assert(test_strrchr(s, 'w') == s + 6);
    assert(test_strrchr(s, 'd') == s + 10);
    assert(test_strrchr(s, 'o') == s + 7);
    assert(test_strrchr(s, 'l') == s + 9);
    assert(test_strrchr(s, 'z') == NULL);
    assert(test_strrchr(s, 'H') == NULL);
    assert(test_strrchr(s, '\0') == s + 11);
    assert(test_strrchr(empty, 'a') == NULL);
    assert(test_strrchr(empty, '\0') == empty);
    assert(test_strrchr(s, 'l') == strrchr(s, 'l'));
    assert(test_strrchr(s, 0) == strrchr(s, 0));
    assert(test_strrchr(empty, 0) == strrchr(empty, 0));

    printf("strrchr tests passed!\n");
}

void run_memcpy_tests(void) {
    printf("Running memcpy tests...\n");

    // Basic memcpy
    {
        char src[] = "Hello World";
        char dest[20] = {0};
        test_memcpy(dest, src, 12);
        ASSERT_MEM_EQ(dest, src, 12, "Basic memcpy failed");
    }

    // Small memcpy consistency
    {
        char src[] = "12345678";
        char dest[10];
        for (int i = 0; i <= 8; i++) {
            memset(dest, 0, sizeof(dest));
            test_memcpy(dest, src, i);
            ASSERT_MEM_EQ(dest, src, i, "Small memcpy consistency check");
        }
    }

    // Unaligned memcpy
    {
        char s_buf[64] __attribute__((aligned(16)));
        char d_buf[64] __attribute__((aligned(16)));
        for (int i = 0; i < 64; i++) s_buf[i] = (char)i;

        memset(d_buf, 0, 64);
        test_memcpy(d_buf + 1, s_buf, 10);
        ASSERT_MEM_EQ(d_buf + 1, s_buf, 10, "Unaligned dest memcpy failed");

        memset(d_buf, 0, 64);
        test_memcpy(d_buf, s_buf + 1, 10);
        ASSERT_MEM_EQ(d_buf, s_buf + 1, 10, "Unaligned src memcpy failed");

        memset(d_buf, 0, 64);
        test_memcpy(d_buf + 1, s_buf + 1, 10);
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
            test_memcpy(dest, src, size);
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
        test_memset(buf, 'A', 100);
        for (int i = 0; i < 100; i++) assert(buf[i] == 'A');
        test_memset(buf, 0, 100);
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
                    test_memset(buf + offset, c, len);
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
            test_memset(buf, 0x55, size);
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
    test_strncpy(dest, src, 8);
    assert(strcmp(dest, "hello") == 0);
    assert(dest[5] == '\0');
    assert(dest[6] == '\0');
    assert(dest[7] == '\0');
    assert(dest[8] == 'X');

    // 2. n == src length (no null termination if exactly fits)
    memset(dest, 'X', sizeof(dest));
    test_strncpy(dest, src, 5);
    assert(memcmp(dest, "hello", 5) == 0);
    assert(dest[5] == 'X');

    // 3. n < src length (truncation)
    memset(dest, 'X', sizeof(dest));
    test_strncpy(dest, src, 3);
    assert(memcmp(dest, "hel", 3) == 0);
    assert(dest[3] == 'X');

    // 4. n = 0
    memset(dest, 'X', sizeof(dest));
    test_strncpy(dest, src, 0);
    assert(dest[0] == 'X');

    // 5. empty src
    char empty[] = "";
    memset(dest, 'X', sizeof(dest));
    test_strncpy(dest, empty, 3);
    assert(dest[0] == '\0');
    assert(dest[1] == '\0');
    assert(dest[2] == '\0');
    assert(dest[3] == 'X');

    printf("strncpy tests passed!\n");
}

int main(void) {
    run_strcmp_tests();
    run_strncmp_tests();
    run_strrchr_tests();
    run_memcpy_tests();
    run_memset_tests();
    run_strncpy_tests();
    return 0;
}
