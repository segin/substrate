#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <setjmp.h>
#include <string.h>
#include <assert.h>

// Mock buffer and jump buffer
static char error_buffer[4096];
static jmp_buf abort_jmp;

// Mock functions
int mock_fprintf(FILE *stream, const char *format, ...);
void mock_abort(void);

// Implementation of mocks
int mock_fprintf(FILE *stream, const char *format, ...) {
    (void)stream; // Unused
    va_list args;
    va_start(args, format);
    vsnprintf(error_buffer, sizeof(error_buffer), format, args);
    va_end(args);
    return 0;
}

void mock_abort(void) {
    longjmp(abort_jmp, 1);
}

// Rename functions to avoid conflict and allow interception
#define fprintf mock_fprintf
#define abort mock_abort
#define __assert_fail tested_assert_fail

// Include the source file under test
#include "../../../lib/c/src/assert.c"

// Helper to reset state
void setup(void) {
    error_buffer[0] = '\0';
}

void test_basic_assertion_fail(void) {
    setup();
    const char *expr = "x > 0";
    const char *file = "test_file.c";
    int line = 42;
    const char *func = "test_func";

    printf("Testing __assert_fail with: expr='%s', file='%s', line=%d, func='%s'\n", expr, file, line, func);

    if (setjmp(abort_jmp) == 0) {
        tested_assert_fail(expr, file, line, func);
        // Should not reach here
        printf("FAIL: __assert_fail did not abort\n");
        exit(1);
    } else {
        // Expected path - mock_abort called longjmp
        // Verify output
        char expected[1024];
        snprintf(expected, sizeof(expected), "Assertion failed: %s (%s: %s: %d)\n", expr, file, func, line);

        if (strcmp(error_buffer, expected) != 0) {
            printf("FAIL: Message mismatch.\nExpected: '%s'\nActual:   '%s'\n", expected, error_buffer);
            exit(1);
        }
        printf("PASS: Basic assertion fail\n");
    }
}

int main(void) {
    printf("Running assert tests...\n");
    test_basic_assertion_fail();
    printf("All assert tests passed!\n");
    return 0;
}
