#include <kern/console.h>
#include <stdio.h>
#include <string.h>

void test_printf_flags(void) {
    char buf[64];
    int fail = 0;

    kprint("Testing printf flags...\n");

    // Helper macro
    #define TEST_PRINTF(fmt, val, expected) do { \
        memset(buf, 0, sizeof(buf)); \
        snprintf(buf, sizeof(buf), fmt, val); \
        if (strcmp(buf, expected) != 0) { \
            kprintf("FAIL: \"" fmt "\" -> \"%s\" (expected \"%s\")\n", buf, expected); \
            fail++; \
        } \
    } while (0)

    // 1. Basic flags (Baseline)
    TEST_PRINTF("%-5d", 42, "42   ");
    TEST_PRINTF("%+d", 42, "+42");
    TEST_PRINTF("% d", 42, " 42");
    TEST_PRINTF("%#x", 0x1a, "0x1a");
    TEST_PRINTF("%05d", 42, "00042");

    // 2. Combinations - Fixed Order (might work now)
    TEST_PRINTF("%-+5d", 42, "+42  ");

    // 3. Combinations - Mixed Order (Expected to fail currently)

    // + and -
    TEST_PRINTF("%+-5d", 42, "+42  ");

    // + and space (+ overrides space)
    TEST_PRINTF("%+ d", 42, "+42");
    TEST_PRINTF("% +d", 42, "+42");

    // - and space
    TEST_PRINTF("%- 5d", 42, " 42  ");
    TEST_PRINTF("% -5d", 42, " 42  ");

    // 0 and - (- overrides 0)
    TEST_PRINTF("%-05d", 42, "42   ");
    TEST_PRINTF("%0-5d", 42, "42   ");

    // 0 and +
    TEST_PRINTF("%+05d", 42, "+0042");
    TEST_PRINTF("%0+5d", 42, "+0042");

    // 0 and space
    TEST_PRINTF("% 05d", 42, " 0042");
    TEST_PRINTF("%0 5d", 42, " 0042");

    // All flags
    // - overrides 0, + overrides space
    // So equivalent to %-+5d -> "+42  "
    TEST_PRINTF("%-+ 05d", 42, "+42  ");
    TEST_PRINTF("%0 + -5d", 42, "+42  ");

    if (fail == 0) {
        kprint("PASS: printf flags tests\n");
    } else {
        kprintf("FAIL: %d printf flags tests failed\n", fail);
    }
}
