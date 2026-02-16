#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>

// Rename implemented functions to avoid conflicts with host libc
#undef strfry
#define strfry libc_strfry
#undef memcpy
#define memcpy libc_memcpy
#undef memmove
#define memmove libc_memmove
#undef memset
#define memset libc_memset
#undef memcmp
#define memcmp libc_memcmp
#undef memchr
#define memchr libc_memchr
#undef strcpy
#define strcpy libc_strcpy
#undef strncpy
#define strncpy libc_strncpy
#undef strcat
#define strcat libc_strcat
#undef strncat
#define strncat libc_strncat
#undef strcmp
#define strcmp libc_strcmp
#undef strncmp
#define strncmp libc_strncmp
#undef strchr
#define strchr libc_strchr
#undef strrchr
#define strrchr libc_strrchr
#undef strstr
#define strstr libc_strstr
#undef strlen
#define strlen libc_strlen
#undef strdup
#define strdup libc_strdup
#undef strspn
#define strspn libc_strspn
#undef strcspn
#define strcspn libc_strcspn
#undef strtok
#define strtok libc_strtok
#undef strpbrk
#define strpbrk libc_strpbrk
#undef strtok_r
#define strtok_r libc_strtok_r
#undef strerror
#define strerror libc_strerror
#undef strcasecmp
#define strcasecmp libc_strcasecmp
#undef strncasecmp
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
        fprintf(stderr, "FAIL: %s: expected %ld, got %ld\n", msg, (long)(expected), (long)(actual)); \
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

#define ASSERT_STREQ(actual, expected, msg) do { \
    const char *act = (actual); \
    const char *exp = (expected); \
    if (act == NULL && exp == NULL) { \
        /* both NULL is OK */ \
    } else if (act == NULL || exp == NULL) { \
        fprintf(stderr, "FAIL: %s: expected '%s', got '%s'\n", msg, exp ? exp : "NULL", act ? act : "NULL"); \
        exit(1); \
    } else if (strcmp(act, exp) != 0) { \
        fprintf(stderr, "FAIL: %s: expected '%s', got '%s'\n", msg, exp, act); \
        exit(1); \
    } \
} while(0)

void run_strlen_tests(void) {
    printf("Running strlen tests...\n");
    ASSERT_EQ(libc_strlen(""), 0, "Empty string");
    ASSERT_EQ(libc_strlen("a"), 1, "Single character");
    ASSERT_EQ(libc_strlen("abc"), 3, "abc length");
    ASSERT_EQ(libc_strlen("hello world"), 11, "hello world length");
    char buf[] = {'h', 'i', '\0', 'x'};
    ASSERT_EQ(libc_strlen(buf), 2, "Embedded null");
}

void run_memmove_tests(void) {
    printf("Running memmove tests...\n");
    char buf[256];
    strcpy(buf, "Hello World");
    memset(buf + 20, 0, 20);
    libc_memmove(buf + 20, buf, 12);
    ASSERT_TRUE(strcmp(buf + 20, "Hello World") == 0, "Non-overlapping forward");
    
    strcpy(buf, "0123456789");
    libc_memmove(buf + 2, buf, 4);
    ASSERT_TRUE(strncmp(buf, "0101236789", 10) == 0, "Overlapping backward");
}

void run_strtok_tests(void) {
    printf("Running strtok tests...\n");
    char str[] = "A string to tokenize";
    const char *delim = " ";
    ASSERT_STREQ(libc_strtok(str, delim), "A", "First token");
    ASSERT_STREQ(libc_strtok(NULL, delim), "string", "Second token");
}

void run_strcat_tests(void) {
    printf("Running strcat tests...\n");
    char dest[20] = "Hello";
    libc_strcat(dest, " World");
    ASSERT_EQ(strcmp(dest, "Hello World"), 0, "Basic strcat");
}

bool test_libc_strlen(void) {
    run_strlen_tests();
    return true;
}

bool test_libc_memmove(void) {
    run_memmove_tests();
    return true;
}

bool test_libc_strcat(void) {
    run_strcat_tests();
    return true;
}

bool test_libc_strtok(void) {
    run_strtok_tests();
    return true;
}

#ifndef NO_MAIN
int main(void) {
    run_strlen_tests();
    run_memmove_tests();
    run_strcat_tests();
    run_strtok_tests();
    return 0;
}
#endif
