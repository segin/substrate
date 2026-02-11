#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>

/* Rename functions to avoid conflicts with host libc */
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

/* Forward declarations */
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

/* Include the implementation directly */
#include "../../../lib/c/src/string.c"

/* Undefine macros to access host functions for verification */
#undef strrchr
/* We can verify against host implementation */

int main(void) {
    const char *s = "hello world";
    const char *empty = "";

    printf("Testing strrchr...\n");

    // 1. Basic search (character exists)
    // "hello world"
    //  01234567890
    assert(my_strrchr(s, 'h') == s);
    assert(my_strrchr(s, 'w') == s + 6);

    // 2. Character at end
    assert(my_strrchr(s, 'd') == s + 10);

    // 3. Multiple occurrences (should find last)
    // 'o' is at 4 and 7
    assert(my_strrchr(s, 'o') == s + 7);
    // 'l' is at 2, 3, 9
    assert(my_strrchr(s, 'l') == s + 9);

    // 4. Character not found
    assert(my_strrchr(s, 'z') == NULL);
    assert(my_strrchr(s, 'H') == NULL); // Case sensitivity

    // 5. Null terminator search
    // Should return pointer to the null terminator
    assert(my_strrchr(s, '\0') == s + 11);

    // 6. Empty string
    assert(my_strrchr(empty, 'a') == NULL);
    assert(my_strrchr(empty, '\0') == empty);

    // 7. Verify against host implementation
    // This ensures behavior matches standard expectations
    assert(my_strrchr(s, 'l') == strrchr(s, 'l'));
    assert(my_strrchr(s, 0) == strrchr(s, 0));
    assert(my_strrchr(empty, 0) == strrchr(empty, 0));

    printf("All tests passed!\n");
    return 0;
}
