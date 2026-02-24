#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <setjmp.h>
#include <stdbool.h>

// Mock variables
static char error_buffer[1024];
static jmp_buf abort_jmp;
static bool abort_called = false;

// Mock implementations
int mock_fprintf(FILE *stream, const char *format, ...) {
    (void)stream; // Ignored, always write to buffer
    va_list args;
    va_start(args, format);
    vsnprintf(error_buffer, sizeof(error_buffer), format, args);
    va_end(args);
    return 0;
}

void mock_abort(void) {
    abort_called = true;
    longjmp(abort_jmp, 1);
}

// Override macros
#define fprintf mock_fprintf
#define abort mock_abort

// Include source file directly
#include "../../../lib/c/src/assert.c"

// Test function
bool test_assert_fail(void) {
    printf("Running test_assert_fail...\n");

    // Clear state
    memset(error_buffer, 0, sizeof(error_buffer));
    abort_called = false;

    // Set up return point for abort()
    if (setjmp(abort_jmp) == 0) {
        // Trigger assertion failure
        __assert_fail("expression", "file.c", 123, "function");

        // Should not reach here
        printf("FAILED: __assert_fail returned!\n");
        return false;
    }

    // Verify abort was called
    if (!abort_called) {
        printf("FAILED: abort() was not called\n");
        return false;
    }

    // Verify error message format
    // Expected: "Assertion failed: expression (file.c: function: 123)\n"
    const char *expected = "Assertion failed: expression (file.c: function: 123)\n";
    if (strcmp(error_buffer, expected) != 0) {
        printf("FAILED: Incorrect error message\n");
        printf("Expected: '%s'\n", expected);
        printf("Actual:   '%s'\n", error_buffer);
        return false;
    }

    printf("PASSED: test_assert_fail\n");
    return true;
}

int main(void) {
    if (test_assert_fail()) {
        printf("All assert tests passed!\n");
        return 0;
    } else {
        printf("Assert tests failed!\n");
        return 1;
    }
}
