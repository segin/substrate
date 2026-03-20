#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>

#ifdef HOST_TEST
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern char *kernel_kvasprintf(const char *fmt, va_list ap);
#define TEST_KVASPRINTF kernel_kvasprintf
extern void kfree(void *ptr, size_t size);

void panic(const char *msg) {
    printf("PANIC: %s\n", msg);
    exit(1);
}

void kprint(const char *msg) {
    printf("%s", msg);
}

#else
#include <kern/console.h>
#include <kern/panic.h>
#include <stdio.h>
#include <string.h>
#include <vm/vm_kmem.h>
#define TEST_KVASPRINTF kvasprintf
#endif

#define ASSERT_STREQ(actual, expected, msg) do { \
    if (strcmp(actual, expected) != 0) { \
        char fail_buf[256]; \
        snprintf(fail_buf, sizeof(fail_buf), "FAIL: %s - Expected '%s', got '%s'", msg, expected, actual); \
        panic(fail_buf); \
    } \
} while(0)

#define ASSERT_NOT_NULL(ptr, msg) do { \
    if ((ptr) == NULL) { \
        char fail_buf[256]; \
        snprintf(fail_buf, sizeof(fail_buf), "FAIL: %s - Pointer is NULL", msg); \
        panic(fail_buf); \
    } \
} while(0)

static char *call_kvasprintf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    char *ret = TEST_KVASPRINTF(fmt, ap);
    va_end(ap);
    return ret;
}

void run_kvasprintf_tests(void);

#ifndef HOST_TEST
bool test_kvasprintf(void) {
    run_kvasprintf_tests();
    return true;
}
#endif

void run_kvasprintf_tests(void) {
    kprint("Running kvasprintf tests...\n");

    char *str;

    // 1. Basic formatting
    str = call_kvasprintf("Hello %s %d", "World", 42);
    ASSERT_NOT_NULL(str, "Basic formatting allocation");
    ASSERT_STREQ(str, "Hello World 42", "Basic formatting content");
    kfree(str, strlen(str) + 1);

    // 2. Empty string
    str = call_kvasprintf("");
    ASSERT_NOT_NULL(str, "Empty string allocation");
    ASSERT_STREQ(str, "", "Empty string content");
    kfree(str, strlen(str) + 1);

    // 3. No arguments
    str = call_kvasprintf("Just a string");
    ASSERT_NOT_NULL(str, "No arguments allocation");
    ASSERT_STREQ(str, "Just a string", "No arguments content");
    kfree(str, strlen(str) + 1);

    // 4. Large string (requires more memory)
    str = call_kvasprintf("%s %s %s %s %s", "A", "B", "C", "D", "E");
    ASSERT_NOT_NULL(str, "Large string allocation");
    ASSERT_STREQ(str, "A B C D E", "Large string content");
    kfree(str, strlen(str) + 1);

    kprint("kvasprintf tests PASS\n");
}
