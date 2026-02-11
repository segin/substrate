#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <stdint.h>

/*
 * Capture host function pointers for reference/verification
 * We use these to verify our implementation behaves correctly (e.g. against glibc)
 */
char *(*host_strncpy)(char *, const char *, size_t) = strncpy;
size_t (*host_strlen)(const char *) = strlen;
int (*host_strcmp)(const char *, const char *) = strcmp;
void *(*host_memset)(void *, int, size_t) = memset;
int (*host_memcmp)(const void *, const void *, size_t) = memcmp;

/*
 * Forward declarations of all functions in string.c
 * This is necessary because some functions call others (e.g. strfry calls strlen)
 * and we need them to be declared before use.
 */
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


/*
 * Rename standard library functions to avoid conflicts with host libc
 * when including the implementation file directly.
 */
#define strfry      my_strfry
#define memcpy      my_memcpy
#define memmove     my_memmove
#define memset      my_memset
#define memcmp      my_memcmp
#define memchr      my_memchr
#define strcpy      my_strcpy
#define strncpy     my_strncpy
#define strcat      my_strcat
#define strncat     my_strncat
#define strcmp      my_strcmp
#define strncmp     my_strncmp
#define strchr      my_strchr
#define strrchr     my_strrchr
#define strstr      my_strstr
#define strlen      my_strlen
#define strdup      my_strdup
#define strspn      my_strspn
#define strcspn     my_strcspn
#define strtok      my_strtok
#define strpbrk     my_strpbrk
#define strtok_r    my_strtok_r
#define strerror    my_strerror

/*
 * Include the implementation under test
 */
#include "../../../lib/c/src/string.c"

/*
 * Test Functions
 */

void test_strncpy_basic() {
    char src[] = "hello";
    char dest[10];

    // Test 1: n > src length (should pad with nulls)
    host_memset(dest, 'X', sizeof(dest));
    my_strncpy(dest, src, 8);

    assert(host_strcmp(dest, "hello") == 0);
    assert(dest[5] == '\0');
    assert(dest[6] == '\0');
    assert(dest[7] == '\0');
    if (dest[8] != 'X') {
        printf("DEBUG: dest[8] is %d ('%c'), expected 'X'\n", dest[8], dest[8]);
        for(int i=0; i<10; i++) printf("%d: %d\n", i, dest[i]);
        fflush(stdout);
    }
    assert(dest[8] == 'X'); // Should not be touched

    // Test 2: n == src length (no null termination if exactly fits)
    host_memset(dest, 'X', sizeof(dest));
    my_strncpy(dest, src, 5);

    assert(host_memcmp(dest, "hello", 5) == 0);
    assert(dest[5] == 'X'); // No null terminator written

    // Test 3: n < src length (truncation)
    host_memset(dest, 'X', sizeof(dest));
    my_strncpy(dest, src, 3);

    assert(host_memcmp(dest, "hel", 3) == 0);
    assert(dest[3] == 'X'); // No null terminator written

    printf("test_strncpy_basic passed\n");
}

void test_strncpy_edge_cases() {
    char src[] = "test";
    char dest[10];

    // Test 4: n = 0 (should do nothing)
    host_memset(dest, 'X', sizeof(dest));
    my_strncpy(dest, src, 0);
    assert(dest[0] == 'X');

    // Test 5: empty src
    char empty[] = "";
    host_memset(dest, 'X', sizeof(dest));
    my_strncpy(dest, empty, 3);
    assert(dest[0] == '\0');
    assert(dest[1] == '\0');
    assert(dest[2] == '\0');
    assert(dest[3] == 'X');

    printf("test_strncpy_edge_cases passed\n");
}

int main() {
    printf("Running string.c tests...\n");
    test_strncpy_basic();
    test_strncpy_edge_cases();
    printf("All tests passed!\n");
    return 0;
}
