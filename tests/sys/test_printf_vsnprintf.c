#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>

#ifdef HOST_TEST
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Mocks and helpers for host test
extern int kernel_vsnprintf(char *str, size_t size, const char *format, va_list ap);
#define TEST_VSNPRINTF kernel_vsnprintf

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
#define TEST_VSNPRINTF vsnprintf
#endif

#define ASSERT_STREQ(actual, expected, msg) do { \
    if (strcmp(actual, expected) != 0) { \
        char fail_buf[256]; \
        snprintf(fail_buf, sizeof(fail_buf), "FAIL: %s - Expected '%s', got '%s'", msg, expected, actual); \
        panic(fail_buf); \
    } \
} while(0)

#define ASSERT_INT_EQ(actual, expected, msg) do { \
    if ((actual) != (expected)) { \
        char fail_buf[256]; \
        snprintf(fail_buf, sizeof(fail_buf), "FAIL: %s - Expected %d, got %d", msg, (int)(expected), (int)(actual)); \
        panic(fail_buf); \
    } \
} while(0)

// Helper to call vsnprintf with variable arguments
static int call_vsnprintf(char *str, size_t size, const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    int ret = TEST_VSNPRINTF(str, size, format, ap);
    va_end(ap);
    return ret;
}

void run_printf_vsnprintf_tests(void);

#ifndef HOST_TEST
bool test_printf_vsnprintf(void) {
    run_printf_vsnprintf_tests();
    return true;
}
#endif

void run_printf_vsnprintf_tests(void) {
    char buf[256];
    int ret;

    kprint("Running vsnprintf tests...\n");

    // 1. Basic formatting
    memset(buf, 'X', sizeof(buf));
    ret = call_vsnprintf(buf, sizeof(buf), "Hello %s %d", "World", 42);
    ASSERT_STREQ(buf, "Hello World 42", "Basic formatting");
    ASSERT_INT_EQ(ret, 14, "Return value for basic formatting");
    ASSERT_INT_EQ(buf[14], '\0', "Null termination basic");
    ASSERT_INT_EQ(buf[15], 'X', "Buffer not overwritten beyond null terminator");

    // 2. Exact fit
    memset(buf, 'X', sizeof(buf));
    ret = call_vsnprintf(buf, 15, "Hello %s %d", "World", 42);
    ASSERT_STREQ(buf, "Hello World 42", "Exact fit formatting");
    ASSERT_INT_EQ(ret, 14, "Return value for exact fit");
    ASSERT_INT_EQ(buf[14], '\0', "Null termination exact fit");
    ASSERT_INT_EQ(buf[15], 'X', "Buffer not overwritten exact fit");

    // 3. Truncation by 1 character
    memset(buf, 'X', sizeof(buf));
    ret = call_vsnprintf(buf, 14, "Hello %s %d", "World", 42);
    ASSERT_STREQ(buf, "Hello World 4", "Truncation by 1 character");
    ASSERT_INT_EQ(ret, 14, "Return value for truncation by 1");
    ASSERT_INT_EQ(buf[13], '\0', "Null termination trunc by 1");
    ASSERT_INT_EQ(buf[14], 'X', "Buffer not overwritten trunc by 1");

    // 4. Heavy truncation
    memset(buf, 'X', sizeof(buf));
    ret = call_vsnprintf(buf, 6, "Hello %s %d", "World", 42);
    ASSERT_STREQ(buf, "Hello", "Heavy truncation");
    ASSERT_INT_EQ(ret, 14, "Return value for heavy truncation");
    ASSERT_INT_EQ(buf[5], '\0', "Null termination heavy trunc");
    ASSERT_INT_EQ(buf[6], 'X', "Buffer not overwritten heavy trunc");

    // 5. Size = 1 (only null terminator)
    memset(buf, 'X', sizeof(buf));
    ret = call_vsnprintf(buf, 1, "Hello %s %d", "World", 42);
    ASSERT_STREQ(buf, "", "Size 1");
    ASSERT_INT_EQ(ret, 14, "Return value for size 1");
    ASSERT_INT_EQ(buf[0], '\0', "Null termination size 1");
    ASSERT_INT_EQ(buf[1], 'X', "Buffer not overwritten size 1");

    // 6. Size = 0 (no writing, return length)
    memset(buf, 'X', sizeof(buf));
    ret = call_vsnprintf(buf, 0, "Hello %s %d", "World", 42);
    ASSERT_INT_EQ(ret, 14, "Return value for size 0");
    ASSERT_INT_EQ(buf[0], 'X', "Buffer not overwritten size 0");

    kprint("vsnprintf tests PASS\n");
}
