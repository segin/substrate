#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <assert.h>

/* Rename functions to avoid conflict with host libc */
#define strfry tested_strfry
#define memcpy tested_memcpy
#define memmove tested_memmove
#define memset tested_memset
#define memcmp tested_memcmp
#define memchr tested_memchr
#define strcpy tested_strcpy
#define strncpy tested_strncpy
#define strcat tested_strcat
#define strncat tested_strncat
#define strcmp tested_strcmp
#define strncmp tested_strncmp
#define strchr tested_strchr
#define strrchr tested_strrchr
#define strstr tested_strstr
#define strlen tested_strlen
#define strdup tested_strdup
#define strspn tested_strspn
#define strcspn tested_strcspn
#define strtok tested_strtok
#define strpbrk tested_strpbrk
#define strtok_r tested_strtok_r
#define strerror tested_strerror

/* Forward declarations to handle dependencies within string.c */
char *tested_strfry(char *string);
void *tested_memcpy(void *dest, const void *src, size_t n);
void *tested_memmove(void *dest, const void *src, size_t n);
void *tested_memset(void *s, int c, size_t n);
int tested_memcmp(const void *s1, const void *s2, size_t n);
void *tested_memchr(const void *s, int c, size_t n);
char *tested_strcpy(char *dest, const char *src);
char *tested_strncpy(char *dest, const char *src, size_t n);
char *tested_strcat(char *dest, const char *src);
char *tested_strncat(char *dest, const char *src, size_t n);
int tested_strcmp(const char *s1, const char *s2);
int tested_strncmp(const char *s1, const char *s2, size_t n);
char *tested_strchr(const char *s, int c);
char *tested_strrchr(const char *s, int c);
char *tested_strstr(const char *haystack, const char *needle);
size_t tested_strlen(const char *s);
char *tested_strdup(const char *s);
size_t tested_strspn(const char *s, const char *accept);
size_t tested_strcspn(const char *s, const char *reject);
char *tested_strtok(char *str, const char *delim);
char *tested_strpbrk(const char *s1, const char *s2);
char *tested_strtok_r(char *str, const char *delim, char **saveptr);
char *tested_strerror(int errnum);

/* Include the source file directly */
#include "../src/string.c"

#undef memset
/* ... undef others if needed, but not strictly required for test logic */

void test_memset_basic(void) {
    char buf[100];

    // Test 1: Fill with 'A'
    tested_memset(buf, 'A', 100);
    for (int i = 0; i < 100; i++) {
        assert(buf[i] == 'A');
    }

    // Test 2: Fill with 0
    tested_memset(buf, 0, 100);
    for (int i = 0; i < 100; i++) {
        assert(buf[i] == 0);
    }

    printf("Basic memset test passed.\n");
}

void test_memset_alignment(void) {
    // Allocate a buffer slightly larger than needed to test alignment
    char *buf = malloc(128);
    if (!buf) {
        perror("malloc");
        exit(1);
    }

    // Test various alignments and sizes
    for (int offset = 0; offset < 8; offset++) {
        for (size_t len = 0; len < 64; len++) {
            // Clear buffer first
            for (int i = 0; i < 128; i++) buf[i] = 0;

            // Expected fill byte
            int c = (offset + len) % 256;

            // Run tested function
            tested_memset(buf + offset, c, len);

            // Verify
            for (int i = 0; i < 128; i++) {
                if (i >= offset && i < offset + (int)len) {
                    if ((unsigned char)buf[i] != (unsigned char)c) {
                        fprintf(stderr, "Mismatch at offset %d (base %d, len %zu): expected %d, got %d\n",
                                i, offset, len, c, (unsigned char)buf[i]);
                        exit(1);
                    }
                } else {
                    if (buf[i] != 0) {
                        fprintf(stderr, "Buffer overwrite at offset %d (base %d, len %zu)\n",
                                i, offset, len);
                        exit(1);
                    }
                }
            }
        }
    }

    free(buf);
    printf("Alignment memset test passed.\n");
}

void test_memset_large(void) {
    size_t size = 1024 * 1024; // 1MB
    char *buf = malloc(size);
    if (!buf) {
        perror("malloc");
        exit(1);
    }

    tested_memset(buf, 0x55, size);

    for (size_t i = 0; i < size; i++) {
        if ((unsigned char)buf[i] != 0x55) {
            fprintf(stderr, "Large buffer mismatch at index %zu\n", i);
            exit(1);
        }
    }

    free(buf);
    printf("Large memset test passed.\n");
}

int main(void) {
    printf("Running memset tests...\n");

    test_memset_basic();
    test_memset_alignment();
    test_memset_large();

    printf("All memset tests passed!\n");
    return 0;
}
