#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <setjmp.h>
#include <string.h>
#include <assert.h>

// Mock state
static char stderr_buffer[1024];
static jmp_buf abort_jmp;
static int abort_called = 0;

// Mock implementations
int mock_fprintf(FILE *stream, const char *format, ...) {
    if (stream != stderr) return 0; // Only capture stderr
    va_list args;
    va_start(args, format);
    vsnprintf(stderr_buffer, sizeof(stderr_buffer), format, args);
    va_end(args);
    return 0;
}

void mock_abort(void) {
    abort_called = 1;
    longjmp(abort_jmp, 1);
}

// Rename functions to avoid conflicts and inject mocks
#define fprintf mock_fprintf
#define abort mock_abort
#define __assert_fail tested_assert_fail

// Include the source file
#include "../../../lib/c/src/assert.c"

// Helper macros
#define ASSERT_STR_CONTAINS(haystack, needle) \
    if (strstr(haystack, needle) == NULL) { \
        printf("FAIL: Expected '%s' to contain '%s'\n", haystack, needle); \
        return 1; \
    }

int main(void) {
    printf("Running assert tests...\n");

    // Test case 1: verify message format
    const char *expr = "x > 0";
    const char *file = "test.c";
    int line = 123;
    const char *func = "main";

    abort_called = 0;
    memset(stderr_buffer, 0, sizeof(stderr_buffer));

    if (setjmp(abort_jmp) == 0) {
        tested_assert_fail(expr, file, line, func);
        // Should not reach here
        printf("FAIL: abort() was not called\n");
        return 1;
    }

    if (!abort_called) {
        printf("FAIL: abort() flag not set\n");
        return 1;
    }

    // Check message content
    // Expected: "Assertion failed: %s (%s: %s: %d)\n"
    // "Assertion failed: x > 0 (test.c: main: 123)\n"
    ASSERT_STR_CONTAINS(stderr_buffer, "Assertion failed");
    ASSERT_STR_CONTAINS(stderr_buffer, expr);
    ASSERT_STR_CONTAINS(stderr_buffer, file);
    ASSERT_STR_CONTAINS(stderr_buffer, func);

    // Check line number (converting to string)
    char line_str[16];
    sprintf(line_str, ": %d)", line);
    ASSERT_STR_CONTAINS(stderr_buffer, line_str);

    printf("All assert tests passed!\n");
    return 0;
}
