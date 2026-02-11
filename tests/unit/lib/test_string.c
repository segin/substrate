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
void *our_memmove(void *dest, const void *src, size_t n);

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

bool test_libc_memmove(void) {
    char buf[256];
    char ref[256];

    // Case 1: Non-overlapping copy (forward)
    // Setup: "Hello World" in buf, copy to buf+20
    strcpy(buf, "Hello World");
    memset(buf + 20, 0, 20); // clear dest
    our_memmove(buf + 20, buf, 12); // copy "Hello World\0"
    if (strcmp(buf + 20, "Hello World") != 0) return false;

    // Case 2: Overlapping copy (backward) - dest > src
    // "0123456789" -> shift first 4 chars to index 2
    // Expected: "0101236789"
    // Src: "0123" (at 0), Dest: at 2.
    // If we copy forward: 0->2 (buf[2]='0'), 1->3 (buf[3]='1'), 2->4 (buf[4]='0' overwritten!), 3->5 ...
    // Backward copy needed.
    strcpy(buf, "0123456789");
    our_memmove(buf + 2, buf, 4);
    // buf should now start with "010123"
    // Check the whole string
    if (strncmp(buf, "0101236789", 10) != 0) return false;

    // Case 3: Overlapping copy (forward) - dest < src
    // "0123456789" -> shift chars at 2..5 to 0
    // Src: "2345" (at 2), Dest: at 0.
    // Expected: "2345456789"
    strcpy(buf, "0123456789");
    our_memmove(buf, buf + 2, 4);
    if (strncmp(buf, "2345456789", 10) != 0) return false;

    // Case 4: Exact overlap (dest == src)
    strcpy(buf, "abcdef");
    our_memmove(buf, buf, 6);
    if (strcmp(buf, "abcdef") != 0) return false;

    // Case 5: Zero length
    strcpy(buf, "abcdef");
    our_memmove(buf + 1, buf, 0);
    if (strcmp(buf, "abcdef") != 0) return false;

    // Case 6: Unaligned access / boundaries
    // Test copying with offsets to ensure alignment handling is correct
    // Initialize buffer with pattern
    for(int i = 0; i < 64; i++) buf[i] = (char)i;

    // Copy 7 bytes from offset 1 to offset 4
    // 1..7 -> 4..10
    // buf[1]=1 ... buf[7]=7
    // expected buf[4]=1 ... buf[10]=7
    our_memmove(buf + 4, buf + 1, 7);

    for(int i = 0; i < 7; i++) {
        if (buf[4 + i] != (char)(i + 1)) return false;
    }

    // Verify surrounding bytes were not touched (simple check)
    if (buf[3] != 3) return false;
    // buf[11] was originally 11. Copy ended at 10.
    if (buf[11] != 11) return false;

    return true;
}
