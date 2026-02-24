#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <setjmp.h>

// Undefine NDEBUG to ensure assert is enabled (though we are testing __assert_fail directly)
#undef NDEBUG
#include <assert.h>

// Mocks
int mock_abort_called = 0;
jmp_buf abort_jmp_buf;

void mock_abort(void) {
    mock_abort_called = 1;
    longjmp(abort_jmp_buf, 1);
}

char mock_fprintf_buffer[1024];
int mock_fprintf_called = 0;
int mock_fprintf(FILE *stream, const char *format, ...) {
    mock_fprintf_called = 1;

    // verify stream is stderr
    if (stream != stderr) {
        printf("FAIL: mock_fprintf called with stream != stderr\n");
        return -1;
    }

    va_list args;
    va_start(args, format);
    vsnprintf(mock_fprintf_buffer, sizeof(mock_fprintf_buffer), format, args);
    va_end(args);
    return 0;
}

// Rename symbols to avoid conflicts and to allow testing
#define __assert_fail tested_assert_fail
#define abort mock_abort
#define fprintf mock_fprintf

// Include the source file
#include "../../../lib/c/src/assert.c"

// Undefine mocks to restore normal behavior for the test runner itself
#undef __assert_fail
#undef abort
#undef fprintf

int main(void) {
    printf("Running assert tests...\n");

    // Test Case 1: Call __assert_fail directly
    mock_abort_called = 0;
    mock_fprintf_called = 0;
    memset(mock_fprintf_buffer, 0, sizeof(mock_fprintf_buffer));

    if (setjmp(abort_jmp_buf) == 0) {
        tested_assert_fail("x == 1", "test_file.c", 123, "test_func");
        // Should not reach here
        printf("FAIL: tested_assert_fail returned without aborting\n");
        return 1;
    }

    assert(mock_abort_called == 1);
    assert(mock_fprintf_called == 1);

    // Check the output message
    // Expected: "Assertion failed: x == 1 (test_file.c: test_func: 123)\n"
    const char *expected = "Assertion failed: x == 1 (test_file.c: test_func: 123)\n";
    if (strcmp(mock_fprintf_buffer, expected) != 0) {
        printf("FAIL: Message mismatch.\nExpected: '%s'\nActual:   '%s'\n", expected, mock_fprintf_buffer);
        return 1;
    }

    printf("test_assert passed\n");
    return 0;
}
