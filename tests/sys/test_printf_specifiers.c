#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>

#ifdef HOST_TEST
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

// Mocks and helpers for host test
extern int kernel_snprintf(char *str, size_t size, const char *format, ...);
#define TEST_SNPRINTF kernel_snprintf

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
#include <stdio.h> // For snprintf prototype in kernel
#include <string.h>
#define TEST_SNPRINTF snprintf
#endif

#define ASSERT_STREQ(actual, expected, msg) do { \
    if (strcmp(actual, expected) != 0) { \
        char fail_buf[256]; \
        TEST_SNPRINTF(fail_buf, sizeof(fail_buf), "FAIL: %s - Expected '%s', got '%s'", msg, expected, actual); \
        panic(fail_buf); \
    } \
} while(0)

#define ASSERT_INT_EQ(actual, expected, msg) do { \
    if ((actual) != (expected)) { \
        char fail_buf[256]; \
        TEST_SNPRINTF(fail_buf, sizeof(fail_buf), "FAIL: %s - Expected %d, got %d", msg, (int)(expected), (int)(actual)); \
        panic(fail_buf); \
    } \
} while(0)

void run_printf_specifier_tests(void) {
    char buf[256];
    int ret;

    kprint("Running comprehensive printf specifier tests...\n");

    // 1. Integer tests (%d, %i)
    // Basic
    ret = TEST_SNPRINTF(buf, sizeof(buf), "%d", 123);
    ASSERT_STREQ(buf, "123", "Basic %d");
    ASSERT_INT_EQ(ret, 3, "Return value %d");

    ret = TEST_SNPRINTF(buf, sizeof(buf), "%i", -456);
    ASSERT_STREQ(buf, "-456", "Basic %i negative");

    // Length modifiers
    ret = TEST_SNPRINTF(buf, sizeof(buf), "%hd", (short)32000);
    ASSERT_STREQ(buf, "32000", "%hd short");

    ret = TEST_SNPRINTF(buf, sizeof(buf), "%hhd", (char)127);
    ASSERT_STREQ(buf, "127", "%hhd char");

    ret = TEST_SNPRINTF(buf, sizeof(buf), "%ld", 1234567890L);
    ASSERT_STREQ(buf, "1234567890", "%ld long");

    ret = TEST_SNPRINTF(buf, sizeof(buf), "%lld", 123456789012345LL);
    ASSERT_STREQ(buf, "123456789012345", "%lld long long");

    // Flags
    ret = TEST_SNPRINTF(buf, sizeof(buf), "%+d", 42);
    ASSERT_STREQ(buf, "+42", "%+d force sign");

    ret = TEST_SNPRINTF(buf, sizeof(buf), "% d", 42);
    ASSERT_STREQ(buf, " 42", "% d space prefix");

    ret = TEST_SNPRINTF(buf, sizeof(buf), "%5d", 42);
    ASSERT_STREQ(buf, "   42", "%5d width");

    ret = TEST_SNPRINTF(buf, sizeof(buf), "%-5d", 42);
    ASSERT_STREQ(buf, "42   ", "%-5d left align");

    ret = TEST_SNPRINTF(buf, sizeof(buf), "%05d", 42);
    ASSERT_STREQ(buf, "00042", "%05d zero pad");

    ret = TEST_SNPRINTF(buf, sizeof(buf), "%.5d", 42);
    ASSERT_STREQ(buf, "00042", "%.5d precision");

    // 2. Unsigned Integer tests (%u)
    ret = TEST_SNPRINTF(buf, sizeof(buf), "%u", 123);
    ASSERT_STREQ(buf, "123", "Basic %u");

    ret = TEST_SNPRINTF(buf, sizeof(buf), "%u", -1); // UINT_MAX
    if (buf[0] == '-') panic("FAIL: %u printed negative number");

    // 3. Octal tests (%o)
    ret = TEST_SNPRINTF(buf, sizeof(buf), "%o", 0123);
    ASSERT_STREQ(buf, "123", "Basic %o");

    ret = TEST_SNPRINTF(buf, sizeof(buf), "%#o", 0123);
    ASSERT_STREQ(buf, "0123", "%#o alternate form");

    // 4. Hex tests (%x, %X)
    ret = TEST_SNPRINTF(buf, sizeof(buf), "%x", 0xabc);
    ASSERT_STREQ(buf, "abc", "Basic %x");

    ret = TEST_SNPRINTF(buf, sizeof(buf), "%X", 0xabc);
    ASSERT_STREQ(buf, "ABC", "Basic %X");

    ret = TEST_SNPRINTF(buf, sizeof(buf), "%#x", 0xabc);
    ASSERT_STREQ(buf, "0xabc", "%#x alternate form");

    ret = TEST_SNPRINTF(buf, sizeof(buf), "%#X", 0xabc);
    ASSERT_STREQ(buf, "0XABC", "%#X alternate form");

    // 5. Floating point tests (%f, %F, %e, %E, %g, %G)
    // Basic
    ret = TEST_SNPRINTF(buf, sizeof(buf), "%.2f", 3.14159);
    ASSERT_STREQ(buf, "3.14", "Basic %.2f");

    ret = TEST_SNPRINTF(buf, sizeof(buf), "%F", 3.14159);
    ASSERT_STREQ(buf, "3.141590", "Basic %F default precision");

    // Exp
    ret = TEST_SNPRINTF(buf, sizeof(buf), "%.2e", 123.456);
    ASSERT_STREQ(buf, "1.23e+02", "Basic %.2e");

    ret = TEST_SNPRINTF(buf, sizeof(buf), "%.2E", 123.456);
    ASSERT_STREQ(buf, "1.23E+02", "Basic %.2E");

    // G/g (shortest representation)
    ret = TEST_SNPRINTF(buf, sizeof(buf), "%g", 0.00001);
    ASSERT_STREQ(buf, "1e-05", "%g small number");

    ret = TEST_SNPRINTF(buf, sizeof(buf), "%g", 123456.0);
    ASSERT_STREQ(buf, "123456", "%g normal number");

    ret = TEST_SNPRINTF(buf, sizeof(buf), "%g", 1234567.0);
    ASSERT_STREQ(buf, "1.23457e+06", "%g large number (default prec 6)");

    // 6. Character and String (%c, %s)
    ret = TEST_SNPRINTF(buf, sizeof(buf), "%c", 'A');
    ASSERT_STREQ(buf, "A", "Basic %c");

    ret = TEST_SNPRINTF(buf, sizeof(buf), "%s", "Hello");
    ASSERT_STREQ(buf, "Hello", "Basic %s");

    ret = TEST_SNPRINTF(buf, sizeof(buf), "%.3s", "Hello");
    ASSERT_STREQ(buf, "Hel", "%.3s precision (truncation)");

    ret = TEST_SNPRINTF(buf, sizeof(buf), "%5s", "Hi");
    ASSERT_STREQ(buf, "   Hi", "%5s width");

    // 7. Pointer (%p)
    void *ptr = (void *)0x1234;
    ret = TEST_SNPRINTF(buf, sizeof(buf), "%p", ptr);
    if (strncmp(buf, "0x", 2) != 0) panic("FAIL: %p does not start with 0x");

    // 8. Dynamic width/precision (*)
    ret = TEST_SNPRINTF(buf, sizeof(buf), "%*d", 5, 12);
    ASSERT_STREQ(buf, "   12", "%*d dynamic width");

    ret = TEST_SNPRINTF(buf, sizeof(buf), "%.*d", 3, 1);
    ASSERT_STREQ(buf, "001", "%.*d dynamic precision");

    // 9. Percent literal
    ret = TEST_SNPRINTF(buf, sizeof(buf), "%%");
    ASSERT_STREQ(buf, "%", "%% literal");

    kprint("Printf specifier tests PASS\n");
}
