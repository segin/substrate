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
#include "../src/string.c"

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

int main(void) {
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
    // '\xff' is 255, '\x01' is 1. 255 - 1 = 254 (> 0)
    // If signed char: -1 - 1 = -2 (< 0)
    char s1[] = "\xff";
    char s2[] = "\x01";
    assert(test_strcmp(s1, s2) > 0);
    assert(test_strcmp(s2, s1) < 0);

    // Verify against host strcmp behavior (sign only)
    const char *h1 = "test string 1";
    const char *h2 = "test string 2";
    int res_test = test_strcmp(h1, h2);
    int res_host = strcmp(h1, h2);

    // Check sign consistency
    if (res_host < 0) assert(res_test < 0);
    else if (res_host > 0) assert(res_test > 0);
    else assert(res_test == 0);

    printf("strcmp tests passed!\n");
    return 0;
}
