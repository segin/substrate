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

void run_strcat_tests(void) {
    printf("Running strcat tests...\n");

    // 1. Basic concatenation
    {
        char dest[20] = "Hello";
        const char *src = " World";
        libc_strcat(dest, src);
        ASSERT_EQ(strcmp(dest, "Hello World"), 0, "Basic strcat failed");
    }

    // 2. Concatenate empty string
    {
        char dest[20] = "Hello World";
        libc_strcat(dest, "");
        ASSERT_EQ(strcmp(dest, "Hello World"), 0, "Concatenate empty string failed");
    }

    // 3. Concatenate to empty string
    {
        char dest[20] = "";
        libc_strcat(dest, "Testing");
        ASSERT_EQ(strcmp(dest, "Testing"), 0, "Concatenate to empty string failed");
    }

    // 4. Return value check
    {
        char dest[20] = "Start";
        char *ret = libc_strcat(dest, "End");
        ASSERT_TRUE(ret == dest, "Return value incorrect");
        ASSERT_EQ(strcmp(dest, "StartEnd"), 0, "Concatenation failed in return value check");
    }

    // 5. Concatenate two empty strings
    {
        char dest[20] = "";
        libc_strcat(dest, "");
        ASSERT_EQ(strcmp(dest, ""), 0, "Concatenate two empty strings failed");
    }

    printf("strcat tests passed!\n");
}

bool test_libc_strlen(void) {
    run_strlen_tests();
    return true;
}

bool test_libc_memmove(void) {
    run_memmove_tests();
    return true;
}

#ifndef NO_MAIN
int main(void) {
    run_strlen_tests();
    run_memmove_tests();
    run_strcat_tests();
    return 0;
}
#endif
