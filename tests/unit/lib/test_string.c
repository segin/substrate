#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

// Mocking needed for lib/c/src/string.c
#define malloc mock_malloc
#define rand mock_rand

static void* mock_malloc(size_t size) { (void)size; return NULL; }
static int mock_rand(void) { return 0; }

// Rename our functions so they don't conflict with host's
#define strlen our_strlen
#define memcpy our_memcpy
#define memset our_memset
#define memmove our_memmove
#define memcmp our_memcmp
#define memchr our_memchr
#define strcpy our_strcpy
#define strncpy our_strncpy
#define strcat our_strcat
#define strncat our_strncat
#define strcmp our_strcmp
#define strncmp our_strncmp
#define strchr our_strchr
#define strrchr our_strrchr
#define strstr our_strstr
#define strdup our_strdup
#define strspn our_strspn
#define strcspn our_strcspn
#define strtok our_strtok
#define strtok_r our_strtok_r
#define strpbrk our_strpbrk
#define strfry our_strfry
#define strerror our_strerror

// Forward declarations to avoid implicit declaration warnings/errors
size_t our_strlen(const char *s);
void *our_memcpy(void *dest, const void *src, size_t n);

// Include the implementation directly
#include "../../../lib/c/src/string.c"

bool test_libc_strlen(void) {
    // Empty string
    if (our_strlen("") != 0) return false;

    // Single character
    if (our_strlen("a") != 1) return false;

    // Regular string
    if (our_strlen("abc") != 3) return false;
    if (our_strlen("hello world") != 11) return false;

    // String with embedded null (should stop at first null)
    char buf[] = {'h', 'i', '\0', 'x'};
    if (our_strlen(buf) != 2) return false;

    // Longer string
    char long_str[1025];
    for(int i = 0; i < 1024; i++) long_str[i] = 'A';
    long_str[1024] = '\0';
    if (our_strlen(long_str) != 1024) return false;

    return true;
}
