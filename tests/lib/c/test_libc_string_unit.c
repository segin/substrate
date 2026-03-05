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

void run_memset_tests(void) {
    printf("Running memset tests...\n");
    char buf[256];

    // Basic functionality
    libc_memset(buf, 'A', 10);
    for (int i = 0; i < 10; i++) {
        ASSERT_EQ(buf[i], 'A', "Basic memset");
    }

    // Zero length
    buf[0] = 'B';
    libc_memset(buf, 'A', 0);
    ASSERT_EQ(buf[0], 'B', "Zero length memset");

    // Unaligned start, short length
    libc_memset(buf, 0, sizeof(buf));
    libc_memset(buf + 1, 'C', 2);
    ASSERT_EQ(buf[0], 0, "Unaligned start before");
    ASSERT_EQ(buf[1], 'C', "Unaligned start pos 1");
    ASSERT_EQ(buf[2], 'C', "Unaligned start pos 2");
    ASSERT_EQ(buf[3], 0, "Unaligned start after");

    // Unaligned start, crossing word boundary
    libc_memset(buf, 0, sizeof(buf));
    libc_memset(buf + 3, 'D', 6);
    ASSERT_EQ(buf[2], 0, "Cross word before");
    for (int i = 3; i < 9; i++) {
        ASSERT_EQ(buf[i], 'D', "Cross word pos");
    }
    ASSERT_EQ(buf[9], 0, "Cross word after");

    // Large fill (word-aligned)
    libc_memset(buf, 0, sizeof(buf));
    libc_memset(buf, 0xAA, 128);
    for (int i = 0; i < 128; i++) {
        ASSERT_EQ((unsigned char)buf[i], 0xAA, "Large word-aligned fill");
    }
    ASSERT_EQ(buf[128], 0, "Large word-aligned fill after");

    // Large fill (unaligned start)
    libc_memset(buf, 0, sizeof(buf));
    libc_memset(buf + 1, 0x55, 128);
    ASSERT_EQ(buf[0], 0, "Large unaligned start before");
    for (int i = 1; i < 129; i++) {
        ASSERT_EQ((unsigned char)buf[i], 0x55, "Large unaligned start fill");
    }
    ASSERT_EQ(buf[129], 0, "Large unaligned start after");
}

void run_memcmp_tests(void) {
    printf("Running memcmp tests...\n");

    // Equal buffers
    ASSERT_EQ(libc_memcmp("abc", "abc", 3), 0, "Equal strings");
    ASSERT_EQ(libc_memcmp("abc", "abd", 2), 0, "Equal prefixes");
    ASSERT_EQ(libc_memcmp("", "", 0), 0, "Empty strings n=0");
    ASSERT_EQ(libc_memcmp("abc", "xyz", 0), 0, "Different strings n=0");

    // Differing buffers
    ASSERT_TRUE(libc_memcmp("abc", "abd", 3) < 0, "abc < abd");
    ASSERT_TRUE(libc_memcmp("abd", "abc", 3) > 0, "abd > abc");

    // Test with null bytes
    unsigned char b1[] = {'a', '\0', 'b'};
    unsigned char b2[] = {'a', '\0', 'c'};
    ASSERT_EQ(libc_memcmp(b1, b2, 2), 0, "Equal with embedded null");
    ASSERT_TRUE(libc_memcmp(b1, b2, 3) < 0, "Different after embedded null");

    // Test at different positions
    ASSERT_TRUE(libc_memcmp("xbc", "abc", 3) > 0, "Different at first byte");
    ASSERT_TRUE(libc_memcmp("axc", "abc", 3) > 0, "Different at middle byte");
    ASSERT_TRUE(libc_memcmp("abx", "abc", 3) > 0, "Different at last byte");

    // Sign verification with large values (ensure unsigned char comparison)
    unsigned char c1 = 0xff;
    unsigned char c2 = 0x7f;
    ASSERT_TRUE(libc_memcmp(&c1, &c2, 1) > 0, "0xff > 0x7f (unsigned)");

    // Test with larger buffers
    char large1[1024], large2[1024];
    memset(large1, 'A', 1024);
    memset(large2, 'A', 1024);
    ASSERT_EQ(libc_memcmp(large1, large2, 1024), 0, "Large equal buffers");
    large2[1023] = 'B';
    ASSERT_TRUE(libc_memcmp(large1, large2, 1024) < 0, "Large buffers differ at end");

    // Identical pointers
    ASSERT_EQ(libc_memcmp(large1, large1, 1024), 0, "Identical pointers");
    ASSERT_EQ(libc_memcmp("abc", "abc", 3), 0, "Identical string pointers");

    // Null pointers with n=0
    ASSERT_EQ(libc_memcmp(NULL, NULL, 0), 0, "NULL pointers n=0");
    ASSERT_EQ(libc_memcmp("abc", NULL, 0), 0, "First pointer valid, second NULL n=0");
    ASSERT_EQ(libc_memcmp(NULL, "abc", 0), 0, "First pointer NULL, second valid n=0");

    // Exact difference values
    ASSERT_EQ(libc_memcmp("a", "b", 1), 'a' - 'b', "Exact numeric diff 'a' - 'b'");
    ASSERT_EQ(libc_memcmp("b", "a", 1), 'b' - 'a', "Exact numeric diff 'b' - 'a'");

    unsigned char x[] = {255};
    unsigned char y[] = {127};
    ASSERT_EQ(libc_memcmp(x, y, 1), 255 - 127, "Exact numeric diff 255 - 127");
}

void run_strdup_tests(void) {
    printf("Running strdup tests...\n");

    // Basic duplication
    const char *orig1 = "Hello, World!";
    char *dup1 = libc_strdup(orig1);
    ASSERT_TRUE(dup1 != NULL, "strdup should not return NULL for valid string");
    ASSERT_TRUE(dup1 != orig1, "strdup should return a new pointer");
    ASSERT_STREQ(dup1, orig1, "Duplicated string should match original");
    free(dup1);

    // Empty string
    const char *orig2 = "";
    char *dup2 = libc_strdup(orig2);
    ASSERT_TRUE(dup2 != NULL, "strdup should not return NULL for empty string");
    ASSERT_TRUE(dup2 != orig2, "strdup should return a new pointer for empty string");
    ASSERT_STREQ(dup2, orig2, "Duplicated empty string should match original");
    free(dup2);
}

void run_strspn_tests(void) {
    printf("Running strspn tests...\n");

    // Empty strings
    ASSERT_EQ(libc_strspn("", ""), 0, "Empty s and empty accept");
    ASSERT_EQ(libc_strspn("", "abc"), 0, "Empty s and non-empty accept");
    ASSERT_EQ(libc_strspn("abc", ""), 0, "Non-empty s and empty accept");

    // Basic functionality
    ASSERT_EQ(libc_strspn("abcde", "abc"), 3, "s starts with accept characters");
    ASSERT_EQ(libc_strspn("abcde", "cba"), 3, "s starts with accept characters, different order");
    ASSERT_EQ(libc_strspn("abcde", "xyz"), 0, "s starts with no accept characters");
    ASSERT_EQ(libc_strspn("abcde", "abcde"), 5, "s consists entirely of accept characters");
    ASSERT_EQ(libc_strspn("abcde", "edcba"), 5, "s consists entirely of accept characters, different order");

    // Multiple occurrences and overlapping characters
    ASSERT_EQ(libc_strspn("abacaba", "ab"), 3, "Multiple occurrences of accept characters");
    ASSERT_EQ(libc_strspn("abacaba", "abc"), 7, "All characters match");

    // Missing matching character early on
    ASSERT_EQ(libc_strspn("abxyz", "ab"), 2, "Mismatch after valid characters");
    ASSERT_EQ(libc_strspn("xyzab", "ab"), 0, "No initial match");

    // Duplicates in accept string
    ASSERT_EQ(libc_strspn("hello", "hlleo"), 5, "Accept has duplicates");
    ASSERT_EQ(libc_strspn("hello", "he"), 2, "Accept has subset");
}

void run_strcpy_tests(void) {
    printf("Running strcpy tests...\n");
    char dest[256];

    // Basic copy
    memset(dest, 'X', sizeof(dest));
    char *ret = libc_strcpy(dest, "Hello World");
    ASSERT_EQ((uintptr_t)ret, (uintptr_t)dest, "strcpy returns destination pointer");
    ASSERT_STREQ(dest, "Hello World", "Basic strcpy");
    ASSERT_EQ(dest[11], '\0', "Null terminator copied");
    ASSERT_EQ(dest[12], 'X', "Did not overwrite past null terminator");

    // Empty string
    memset(dest, 'X', sizeof(dest));
    ret = libc_strcpy(dest, "");
    ASSERT_EQ((uintptr_t)ret, (uintptr_t)dest, "strcpy returns destination pointer for empty string");
    ASSERT_STREQ(dest, "", "Empty strcpy");
    ASSERT_EQ(dest[0], '\0', "Null terminator copied for empty string");
    ASSERT_EQ(dest[1], 'X', "Did not overwrite past null terminator for empty string");

    // Large string
    char large_src[128];
    memset(large_src, 'A', 127);
    large_src[127] = '\0';
    memset(dest, 'X', sizeof(dest));
    libc_strcpy(dest, large_src);
    ASSERT_STREQ(dest, large_src, "Large strcpy");
}

bool test_libc_strcpy(void) {
    run_strcpy_tests();
    return true;
}

void run_strstr_tests(void) {
    printf("Running strstr tests...\n");

    // Basic tests
    ASSERT_STREQ(libc_strstr("hello world", "world"), "world", "Basic substring at end");
    ASSERT_STREQ(libc_strstr("hello world", "hello"), "hello world", "Basic substring at start");
    ASSERT_STREQ(libc_strstr("hello world", "lo w"), "lo world", "Basic substring in middle");

    // Empty strings
    ASSERT_STREQ(libc_strstr("hello", ""), "hello", "Empty needle");
    ASSERT_EQ((uintptr_t)libc_strstr("", "world"), 0, "Empty haystack");
    ASSERT_STREQ(libc_strstr("", ""), "", "Both empty");

    // Substring not found
    ASSERT_EQ((uintptr_t)libc_strstr("hello", "world"), 0, "Substring not found");
    ASSERT_EQ((uintptr_t)libc_strstr("he", "hello"), 0, "Needle longer than haystack");

    // Overlapping / repeated substrings
    ASSERT_STREQ(libc_strstr("aaaa", "aa"), "aaaa", "Overlapping substrings");
    ASSERT_STREQ(libc_strstr("mississippi", "issip"), "issippi", "Complex overlap");
}

void run_memcpy_tests(void) {
    printf("Running memcpy tests...\n");

    // Basic copy
    char src1[] = "Hello";
    char dest1[10] = {0};
    libc_memcpy(dest1, src1, 6);
    ASSERT_STREQ(dest1, "Hello", "Basic copy");

    // Empty copy
    char dest2[10] = "world";
    libc_memcpy(dest2, src1, 0);
    ASSERT_STREQ(dest2, "world", "Zero length copy");

    // Test different alignments
    // Create large buffers to ensure word-aligned copying is triggered
    // Use __attribute__((aligned(8))) to guarantee determinism
    char src3[256] __attribute__((aligned(8)));
    char dest3[256] __attribute__((aligned(8)));
    for (int i = 0; i < 256; i++) {
        src3[i] = (char)(i & 0xFF);
    }

    // Aligned to Aligned
    memset(dest3, 0, sizeof(dest3));
    libc_memcpy(dest3, src3, 128);
    ASSERT_MEM_EQ(dest3, src3, 128, "Aligned to aligned copy");

    // Unaligned src to Aligned dest
    memset(dest3, 0, sizeof(dest3));
    libc_memcpy(dest3, src3 + 1, 128);
    ASSERT_MEM_EQ(dest3, src3 + 1, 128, "Unaligned src to aligned dest");

    // Aligned src to Unaligned dest
    memset(dest3, 0, sizeof(dest3));
    libc_memcpy(dest3 + 1, src3, 128);
    ASSERT_MEM_EQ(dest3 + 1, src3, 128, "Aligned src to unaligned dest");

    // Unaligned src to Unaligned dest (same offset)
    memset(dest3, 0, sizeof(dest3));
    libc_memcpy(dest3 + 1, src3 + 1, 128);
    ASSERT_MEM_EQ(dest3 + 1, src3 + 1, 128, "Unaligned to unaligned (same offset)");

    // Unaligned src to Unaligned dest (different offset)
    memset(dest3, 0, sizeof(dest3));
    libc_memcpy(dest3 + 2, src3 + 1, 128);
    ASSERT_MEM_EQ(dest3 + 2, src3 + 1, 128, "Unaligned to unaligned (diff offset)");

    // Test tail bytes (lengths that are not multiples of 4)
    memset(dest3, 0, sizeof(dest3));
    libc_memcpy(dest3, src3, 127); // 127 is not multiple of 4
    ASSERT_MEM_EQ(dest3, src3, 127, "Length not multiple of 4");

    // Partial overlap (memcpy is technically undefined for this in standard C,
    // but the task specifically requested testing it, so we check that it
    // doesn't crash and behaves consistently with a forward copy).
    // Partial overlap - backward: src > dest
    // In standard C, this is undefined, but practically it acts like memmove
    // when copying backward since string.c iterates forwards.
    char overlap_bwd[32] = "0123456789";
    libc_memcpy(overlap_bwd, overlap_bwd + 2, 8);
    ASSERT_STREQ(overlap_bwd, "2345678989", "Backward overlap copy");

    // Partial overlap - forward: dest > src
    // Here we just test it completes. The result is implementation-defined
    // because standard memcpy overwrites src while copying forward.
    char overlap_fwd[32] = "0123456789";
    libc_memcpy(overlap_fwd + 2, overlap_fwd, 8);
    // We simply assert it executed without faults, we don't assert the result
    // as it depends on exact word-size logic.
}

bool test_libc_memcpy(void) {
    run_memcpy_tests();
    return true;
}

void run_strncasecmp_tests(void) {
    printf("Running strncasecmp tests...\n");

    // Equal strings, same casing
    ASSERT_EQ(libc_strncasecmp("abc", "abc", 3), 0, "Equal strings same casing");
    ASSERT_EQ(libc_strncasecmp("abc", "abc", 5), 0, "Equal strings same casing n>len");

    // Equal strings, different casing
    ASSERT_EQ(libc_strncasecmp("AbC", "aBc", 3), 0, "Equal strings different casing");
    ASSERT_EQ(libc_strncasecmp("ABC", "abc", 3), 0, "Equal strings different casing");
    ASSERT_EQ(libc_strncasecmp("abc", "ABC", 3), 0, "Equal strings different casing");

    // Differing strings
    ASSERT_TRUE(libc_strncasecmp("abc", "abd", 3) < 0, "abc < abd");
    ASSERT_TRUE(libc_strncasecmp("abd", "abc", 3) > 0, "abd > abc");
    ASSERT_TRUE(libc_strncasecmp("AbC", "aBd", 3) < 0, "AbC < aBd");

    // Differing strings, matching prefix up to n
    ASSERT_EQ(libc_strncasecmp("abcd", "abce", 3), 0, "Matching prefix up to n");
    ASSERT_EQ(libc_strncasecmp("aBcD", "AbCe", 3), 0, "Matching prefix up to n different casing");

    // Test with n=0
    ASSERT_EQ(libc_strncasecmp("abc", "def", 0), 0, "Differing strings n=0");
    ASSERT_EQ(libc_strncasecmp("", "", 0), 0, "Empty strings n=0");

    // Empty strings
    ASSERT_EQ(libc_strncasecmp("", "", 1), 0, "Empty strings n=1");
    ASSERT_TRUE(libc_strncasecmp("a", "", 1) > 0, "a > empty string");
    ASSERT_TRUE(libc_strncasecmp("", "a", 1) < 0, "empty string < a");

    // Sign verification with large values (ensure unsigned char comparison)
    unsigned char c1[] = {0xff, '\0'};
    unsigned char c2[] = {0x7f, '\0'};
    ASSERT_TRUE(libc_strncasecmp((char*)c1, (char*)c2, 1) > 0, "0xff > 0x7f (unsigned)");
}

bool test_libc_strlen(void) {
    run_strlen_tests();
    return true;
}

bool test_libc_strncasecmp(void) {
    run_strncasecmp_tests();
    return true;
}

bool test_libc_strstr(void) {
    run_strstr_tests();
    return true;
}

bool test_libc_strspn(void) {
    run_strspn_tests();
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

void run_strchr_tests(void) {
    printf("Running strchr tests...\n");
    char buf[] = "Hello World";

    // Basic finding
    ASSERT_EQ((uintptr_t)libc_strchr(buf, 'W'), (uintptr_t)(buf + 6), "Find character in middle");
    ASSERT_EQ((uintptr_t)libc_strchr(buf, 'H'), (uintptr_t)buf, "Find character at start");
    ASSERT_EQ((uintptr_t)libc_strchr(buf, 'd'), (uintptr_t)(buf + 10), "Find character at end");

    // Character not in string
    ASSERT_EQ((uintptr_t)libc_strchr(buf, 'z'), (uintptr_t)NULL, "Character not found");

    // Finding null terminator
    ASSERT_EQ((uintptr_t)libc_strchr(buf, '\0'), (uintptr_t)(buf + 11), "Find null terminator");

    // Empty string
    char empty[] = "";
    ASSERT_EQ((uintptr_t)libc_strchr(empty, 'a'), (uintptr_t)NULL, "Empty string, character not found");
    ASSERT_EQ((uintptr_t)libc_strchr(empty, '\0'), (uintptr_t)empty, "Empty string, find null terminator");

    // Integer conversion (character value > 255)
    ASSERT_EQ((uintptr_t)libc_strchr(buf, 'W' + 256), (uintptr_t)(buf + 6), "Integer conversion > 255");

    // Multiple occurrences (finds first)
    char multiple[] = "abacaba";
    ASSERT_EQ((uintptr_t)libc_strchr(multiple, 'a'), (uintptr_t)multiple, "First occurrence of 'a'");
    ASSERT_EQ((uintptr_t)libc_strchr(multiple, 'b'), (uintptr_t)(multiple + 1), "First occurrence of 'b'");
}

void run_strcmp_tests(void) {
    printf("Running strcmp tests...\n");

    // Equal strings
    ASSERT_EQ(libc_strcmp("abc", "abc"), 0, "Equal strings");
    ASSERT_EQ(libc_strcmp("", ""), 0, "Empty strings");
    ASSERT_EQ(libc_strcmp("a", "a"), 0, "Single character equal");

    // Differing strings
    ASSERT_TRUE(libc_strcmp("abc", "abd") < 0, "abc < abd");
    ASSERT_TRUE(libc_strcmp("abd", "abc") > 0, "abd > abc");

    // Different lengths
    ASSERT_TRUE(libc_strcmp("abc", "ab") > 0, "abc > ab");
    ASSERT_TRUE(libc_strcmp("ab", "abc") < 0, "ab < abc");
    ASSERT_TRUE(libc_strcmp("a", "") > 0, "a > empty");
    ASSERT_TRUE(libc_strcmp("", "a") < 0, "empty < a");

    // Test at different positions
    ASSERT_TRUE(libc_strcmp("xbc", "abc") > 0, "Different at first byte");
    ASSERT_TRUE(libc_strcmp("axc", "abc") > 0, "Different at middle byte");
    ASSERT_TRUE(libc_strcmp("abx", "abc") > 0, "Different at last byte");

    // Sign verification (ensure unsigned char comparison)
    char s1[] = {(char)0xff, '\0'};
    char s2[] = {(char)0x7f, '\0'};
    ASSERT_TRUE(libc_strcmp(s1, s2) > 0, "0xff > 0x7f (unsigned)");
}

void run_strpbrk_tests(void) {
    printf("Running strpbrk tests...\n");

    const char *str = "hello world";

    // Character found
    ASSERT_STREQ(libc_strpbrk(str, "w"), "world", "Find single character");
    ASSERT_STREQ(libc_strpbrk(str, "ol"), "llo world", "Find first of multiple characters");
    ASSERT_STREQ(libc_strpbrk(str, "d"), "d", "Find character at end");
    ASSERT_STREQ(libc_strpbrk(str, "h"), "hello world", "Find character at beginning");

    // Character not found
    ASSERT_EQ((void*)libc_strpbrk(str, "xyz"), NULL, "Characters not in string");

    // Empty strings
    ASSERT_EQ((void*)libc_strpbrk("", "abc"), NULL, "Empty search string");
    ASSERT_EQ((void*)libc_strpbrk(str, ""), NULL, "Empty accept string");
    ASSERT_EQ((void*)libc_strpbrk("", ""), NULL, "Both strings empty");
}

void run_strncmp_tests(void) {
    printf("Running strncmp tests...\n");

    // Equal strings
    ASSERT_EQ(libc_strncmp("abc", "abc", 5), 0, "Equal strings, n > len");
    ASSERT_EQ(libc_strncmp("abc", "abc", 3), 0, "Equal strings, n == len");
    ASSERT_EQ(libc_strncmp("abc", "abcd", 3), 0, "Prefix match, n < len");
    ASSERT_EQ(libc_strncmp("abcd", "abc", 3), 0, "Prefix match, n < len (reverse)");

    // Differing strings
    ASSERT_TRUE(libc_strncmp("abc", "abd", 3) < 0, "abc < abd");
    ASSERT_TRUE(libc_strncmp("abd", "abc", 3) > 0, "abd > abc");
    ASSERT_EQ(libc_strncmp("abc", "abd", 2), 0, "Diff after n");

    // Empty strings
    ASSERT_EQ(libc_strncmp("", "", 5), 0, "Empty strings");
    ASSERT_TRUE(libc_strncmp("a", "", 1) > 0, "a > empty");
    ASSERT_TRUE(libc_strncmp("", "a", 1) < 0, "empty < a");

    // n=0
    ASSERT_EQ(libc_strncmp("abc", "xyz", 0), 0, "n=0");

    // Test with embedded nulls
    ASSERT_EQ(libc_strncmp("a\0b", "a\0c", 3), 0, "Equal with embedded null (stops at null)");

    // Sign verification with large values (ensure unsigned char comparison)
    ASSERT_TRUE(libc_strncmp("\xff", "\x7f", 1) > 0, "0xff > 0x7f (unsigned)");
}

bool test_libc_memset(void) {
    run_memset_tests();
    return true;
}

bool test_libc_memcmp(void) {
    run_memcmp_tests();
    return true;
}

bool test_libc_strchr(void) {
    run_strchr_tests();
    return true;
}

void run_strrchr_tests(void) {
    printf("Running strrchr tests...\n");
    const char *str = "hello world";

    ASSERT_STREQ(libc_strrchr(str, 'o'), "orld", "Find 'o' (multiple occurrences, returns last)");
    ASSERT_STREQ(libc_strrchr(str, 'h'), "hello world", "Find 'h' (first char)");
    ASSERT_STREQ(libc_strrchr(str, 'd'), "d", "Find 'd' (last char)");
    ASSERT_STREQ(libc_strrchr(str, 'z'), NULL, "Find 'z' (not present)");
    ASSERT_STREQ(libc_strrchr(str, '\0'), "", "Find null terminator");
    ASSERT_STREQ(libc_strrchr(str, 'l'), "ld", "Find 'l' (multiple occurrences)");

    const char *empty = "";
    ASSERT_STREQ(libc_strrchr(empty, 'a'), NULL, "Empty string, not found");
    ASSERT_STREQ(libc_strrchr(empty, '\0'), "", "Empty string, null terminator");
}

bool test_libc_strrchr(void) {
    run_strrchr_tests();
    return true;
}

bool test_libc_strcmp(void) {
    run_strcmp_tests();
    return true;
}

bool test_libc_strpbrk(void) {
    run_strpbrk_tests();
    return true;
}

bool test_libc_strncmp(void) {
    run_strncmp_tests();
    return true;
}

bool test_libc_strdup(void) {
    run_strdup_tests();
    return true;
}

#ifndef NO_MAIN
int main(void) {
    run_strcpy_tests();
    run_memcpy_tests();
    run_strlen_tests();
    run_strspn_tests();
    run_memmove_tests();
    run_strcat_tests();
    run_strtok_tests();
    run_memset_tests();
    run_memcmp_tests();
    run_strncasecmp_tests();
    run_strchr_tests();
    run_strstr_tests();
    run_strrchr_tests();
    run_strcmp_tests();
    run_strpbrk_tests();
    run_strncmp_tests();
    run_strdup_tests();
    return 0;
}
#endif
