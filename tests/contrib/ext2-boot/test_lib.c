#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/*
 * Mock the baremetal environment for contrib/ext2-boot/stage2/lib.c
 */

// Basic types and definitions
#define size_t boot_size_t
typedef uint32_t boot_size_t;

#undef NULL
#define NULL ((void*)0)

// Mock HEAP for malloc
uint32_t HEAP = 0;
uint32_t HEAP_START = 0;
static uint8_t simulated_heap[1024 * 1024];

void* boot_malloc(boot_size_t n) {
    void* p = &simulated_heap[HEAP];
    HEAP += n;
    return p;
}

// Redirect stage2 functions to avoid conflicts with host libc
#define strlen boot_strlen
#define strncat boot_strncat
#define strcat boot_strcat
#define strncpy boot_strncpy
#define strcpy boot_strcpy
#define strncmp boot_strncmp
#define strcmp boot_strcmp
#define strchr boot_strchr
#define strdup boot_strdup
#define strrev boot_strrev
#define memcpy boot_memcpy
#define memset boot_memset
#define memsetw boot_memsetw
#define memmove boot_memmove
#define memchr boot_memchr
#define memrchr boot_memrchr
#define memcmp boot_memcmp
#define strtok boot_strtok
#define itoa boot_itoa
#define inb boot_inb
#define outb boot_outb
#define insl boot_insl

// Mock vga_pretty for assert
#define vga_pretty boot_vga_pretty
#define VGA_RED 4
void boot_vga_pretty(const char* msg, int color) {
    fprintf(stderr, "VGA_PRETTY: %s (color: %d)\n", msg, color);
    exit(1);
}

// Disable inline assembly and other keywords that might upset the host compiler
#define __asm__ volatile(...)
#define asm(...)
#define volatile
#define inline

// Let's use a trick: define a guard for defs.h so it's not included.
#define __defs__

// Now provide what defs.h would have provided
#define malloc(n) boot_malloc(n)
#define free(x)
#define assert(e)	((e) ? (void) 0 : vga_pretty(#e, VGA_RED))

#include "../../../contrib/ext2-boot/stage2/lib.c"

// Test helpers
int failures = 0;

#define EXPECT_STREQ(actual, expected) do { \
    if (strcmp(actual, expected) != 0) { \
        fprintf(stderr, "FAIL: %s:%d: Expected \"%s\", got \"%s\"\n", __FILE__, __LINE__, expected, actual); \
        failures++; \
    } \
} while(0)

#define EXPECT_EQ(actual, expected) do { \
    if ((actual) != (expected)) { \
        fprintf(stderr, "FAIL: %s:%d: Expected %lld, got %lld\n", __FILE__, __LINE__, (long long)(expected), (long long)(actual)); \
        failures++; \
    } \
} while(0)

#define EXPECT_PTR_EQ(actual, expected) do { \
    if ((actual) != (expected)) { \
        fprintf(stderr, "FAIL: %s:%d: Expected pointer %p, got %p\n", __FILE__, __LINE__, (void*)(expected), (void*)(actual)); \
        failures++; \
    } \
} while(0)

void test_strlen() {
    printf("  strlen\n");
    EXPECT_EQ(boot_strlen(""), 0);
    EXPECT_EQ(boot_strlen("a"), 1);
    EXPECT_EQ(boot_strlen("abc"), 3);
}

void test_strcat() {
    printf("  strcat/strncat\n");
    char buf[32];

    // Reset buffer each time
    memset(buf, 0, 32);
    strcpy(buf, "hello");
    boot_strcat(buf, " world");
    EXPECT_STREQ(buf, "hello world");

    memset(buf, 0, 32);
    strcpy(buf, "foo");
    boot_strncat(buf, "barbaz", 3);
    EXPECT_STREQ(buf, "foobar");
}

void test_strcpy() {
    printf("  strcpy/strncpy\n");
    char buf[32];

    memset(buf, 0, 32);
    boot_strcpy(buf, "test");
    EXPECT_STREQ(buf, "test");

    memset(buf, 'X', 32);
    boot_strncpy(buf, "hello", 3);
    EXPECT_EQ(buf[0], 'h');
    EXPECT_EQ(buf[1], 'e');
    EXPECT_EQ(buf[2], 'l');
    EXPECT_EQ(buf[3], 'X'); // strncpy doesn't null terminate if n is reached

    memset(buf, 'X', 32);
    boot_strncpy(buf, "hi", 5);
    EXPECT_EQ(buf[0], 'h');
    EXPECT_EQ(buf[1], 'i');
    EXPECT_EQ(buf[2], '\0');
    EXPECT_EQ(buf[3], '\0');
    EXPECT_EQ(buf[4], '\0');
    EXPECT_EQ(buf[5], 'X');
}

void test_strcmp() {
    printf("  strcmp/strncmp\n");
    // Standard strcmp("abc", "abc") = 0
    EXPECT_EQ(boot_strcmp("abc", "abc"), 0);

    // With my fix, strcmp("abc", "abcd") should be mismatch.
    EXPECT_EQ(boot_strcmp("abc", "abcd") != 0, 1);
    EXPECT_EQ(boot_strcmp("abcd", "abc") != 0, 1);

    EXPECT_EQ(boot_strncmp("abc", "abd", 2), 0);
    EXPECT_EQ(boot_strncmp("abc", "abd", 3) != 0, 1);
}

void test_strchr() {
    printf("  strchr\n");
    const char* s = "abcdef";
    // NOTE: boot_strchr returns pointer to AFTER the character found (non-standard)
    EXPECT_PTR_EQ(boot_strchr(s, 'c'), s + 3);
    EXPECT_PTR_EQ(boot_strchr(s, 'z'), NULL);
}

void test_strrev() {
    printf("  strrev\n");
    char s1[] = "hello";
    boot_strrev(s1);
    EXPECT_STREQ(s1, "olleh");

    char s3[] = "a";
    boot_strrev(s3);
    EXPECT_STREQ(s3, "a");
}

void test_mem() {
    printf("  memcpy/memset/memmove/memcmp\n");
    char buf1[32];
    char buf2[32];

    boot_memset(buf1, 0xAA, 10);
    for(int i=0; i<10; i++) EXPECT_EQ((uint8_t)buf1[i], 0xAA);

    boot_memcpy(buf2, buf1, 10);
    EXPECT_EQ(boot_memcmp((uint8_t*)buf1, (uint8_t*)buf2, 10), 0);

    char move_buf[] = "123456";
    boot_memmove(move_buf + 2, move_buf, 3);
    // 123456 -> 12[123]6 -> 121236 (lib.c memmove is forward copy via temp buffer)
    EXPECT_EQ(memcmp(move_buf, "121236", 6), 0);

    uint16_t wbuf[10];
    boot_memsetw(wbuf, 0x1234, 5);
    for(int i=0; i<5; i++) EXPECT_EQ(wbuf[i], 0x1234);
}

void test_strtok() {
    printf("  strtok\n");
    char s[] = "part1,part2";
    char* t = boot_strtok(s, ",");
    EXPECT_STREQ(t, "part1");
}

void test_itoa() {
    printf("  itoa\n");
    char* s = boot_itoa(123, 10);
    EXPECT_STREQ(s, "123");

    // itoa for base 16 prepends zeros up to len=8
    EXPECT_STREQ(boot_itoa(0xABC, 16), "00000ABC");
}

int main() {
    printf("Running bootloader stage2 lib tests...\n");
    test_strlen();
    test_strcat();
    test_strcpy();
    test_strcmp();
    test_strchr();
    test_strrev();
    test_mem();
    test_strtok();
    test_itoa();

    if (failures == 0) {
        printf("All tests passed!\n");
        return 0;
    } else {
        printf("%d tests failed.\n", failures);
        return 1;
    }
}
