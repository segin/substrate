#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>

// Rename functions to avoid conflicts with host libc
#define strfry my_strfry
#define memcpy my_memcpy
#define memmove my_memmove
#define memset my_memset
#define memcmp my_memcmp
#define memchr my_memchr
#define strcpy my_strcpy
#define strncpy my_strncpy
#define strcat my_strcat
#define strncat my_strncat
#define strcmp my_strcmp
#define strncmp my_strncmp
#define strchr my_strchr
#define strrchr my_strrchr
#define strstr my_strstr
#define strlen my_strlen
#define strdup my_strdup
#define strspn my_strspn
#define strcspn my_strcspn
#define strtok my_strtok
#define strpbrk my_strpbrk
#define strtok_r my_strtok_r
#define strerror my_strerror

// Forward declarations to handle internal dependencies (e.g. strfry calling strlen)
char *my_strfry(char *string);
void *my_memcpy(void *dest, const void *src, size_t n);
void *my_memmove(void *dest, const void *src, size_t n);
void *my_memset(void *s, int c, size_t n);
int my_memcmp(const void *s1, const void *s2, size_t n);
void *my_memchr(const void *s, int c, size_t n);
char *my_strcpy(char *dest, const char *src);
char *my_strncpy(char *dest, const char *src, size_t n);
char *my_strcat(char *dest, const char *src);
char *my_strncat(char *dest, const char *src, size_t n);
int my_strcmp(const char *s1, const char *s2);
int my_strncmp(const char *s1, const char *s2, size_t n);
char *my_strchr(const char *s, int c);
char *my_strrchr(const char *s, int c);
char *my_strstr(const char *haystack, const char *needle);
size_t my_strlen(const char *s);
char *my_strdup(const char *s);
size_t my_strspn(const char *s, const char *accept);
size_t my_strcspn(const char *s, const char *reject);
char *my_strtok(char *str, const char *delim);
char *my_strpbrk(const char *s1, const char *s2);
char *my_strtok_r(char *str, const char *delim, char **saveptr);
char *my_strerror(int errnum);

// Include the source file directly
#include "../src/string.c"

// Helper macro for testing
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

int main() {
    printf("Running strncmp tests...\n");

    // 1. Equal strings, n > length
    ASSERT_EQ(my_strncmp("abc", "abc", 5), 0, "Equal strings, n > len");

    // 2. Equal strings, n == length
    ASSERT_EQ(my_strncmp("abc", "abc", 3), 0, "Equal strings, n == len");

    // 3. Equal strings, n < length
    ASSERT_EQ(my_strncmp("abc", "abc", 2), 0, "Equal strings, n < len");

    // 4. Unequal strings, difference at first char
    ASSERT_TRUE(my_strncmp("abc", "bbc", 3) < 0, "abc < bbc");
    ASSERT_TRUE(my_strncmp("bbc", "abc", 3) > 0, "bbc > abc");

    // 5. Unequal strings, difference at last checked char
    ASSERT_TRUE(my_strncmp("abc", "abd", 3) < 0, "abc < abd");

    // 6. Strings differ AFTER n chars
    ASSERT_EQ(my_strncmp("abc", "abd", 2), 0, "Difference after n ignored");

    // 7. One string is prefix of another
    ASSERT_TRUE(my_strncmp("abc", "abcd", 4) < 0, "abc < abcd");
    ASSERT_TRUE(my_strncmp("abcd", "abc", 4) > 0, "abcd > abc");

    // 8. One string is prefix, but n limits check to common part
    ASSERT_EQ(my_strncmp("abc", "abcd", 3), 0, "Prefix match within n");

    // 9. Zero length check
    ASSERT_EQ(my_strncmp("abc", "def", 0), 0, "n=0 should return 0");

    // 10. Empty strings
    ASSERT_EQ(my_strncmp("", "", 5), 0, "Empty strings equal");
    ASSERT_TRUE(my_strncmp("", "a", 5) < 0, "Empty < a");
    ASSERT_TRUE(my_strncmp("a", "", 5) > 0, "a > Empty");

    // 11. Unsigned char comparison check
    // '\200' is 128 (unsigned). If signed char, it's -128. 'a' is 97.
    // If signed comparison: -128 < 97.
    // If unsigned comparison: 128 > 97.
    // Standard requires unsigned comparison.
    char s1[] = {'\200', 0};
    char s2[] = {'a', 0};
    ASSERT_TRUE(my_strncmp(s1, s2, 1) > 0, "Unsigned char comparison");

    printf("All strncmp tests passed.\n");
    return 0;
}
