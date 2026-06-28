#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdarg.h>
#include "num.h"

char mock_stderr[1024];
int mock_stderr_pos = 0;

char mock_stdout[1024];
int mock_stdout_pos = 0;

int mock_fprintf(FILE *stream, const char *format, ...) {
    if (stream == stderr) {
        va_list args;
        va_start(args, format);
        int ret = vsnprintf(mock_stderr + mock_stderr_pos, sizeof(mock_stderr) - mock_stderr_pos, format, args);
        mock_stderr_pos += ret;
        va_end(args);
        return ret;
    } else {
        va_list args;
        va_start(args, format);
        int ret = vfprintf(stream, format, args);
        va_end(args);
        return ret;
    }
}

int mock_vfprintf(FILE *stream, const char *format, va_list ap) {
    if (stream == stderr) {
        int ret = vsnprintf(mock_stderr + mock_stderr_pos, sizeof(mock_stderr) - mock_stderr_pos, format, ap);
        mock_stderr_pos += ret;
        return ret;
    } else {
        return vfprintf(stream, format, ap);
    }
}

int mock_printf(const char *format, ...) {
    va_list args;
    va_start(args, format);
    int ret = vsnprintf(mock_stdout + mock_stdout_pos, sizeof(mock_stdout) - mock_stdout_pos, format, args);
    mock_stdout_pos += ret;
    va_end(args);
    return ret;
}

#define fprintf mock_fprintf
#define vfprintf mock_vfprintf
#define printf mock_printf

#include "../../../usr.lib/bc/num.c"

#undef fprintf
#undef vfprintf
#undef printf

void reset_mocks() {
    mock_stderr_pos = 0;
    mock_stderr[0] = '\0';
    mock_stdout_pos = 0;
    mock_stdout[0] = '\0';
}

void test_bc_error() {
    reset_mocks();

    bc_error("test error %d", 123);

    assert(strstr(mock_stderr, "bc: runtime error: ") != NULL);
    assert(strstr(mock_stderr, "test error 123") != NULL);
    assert(strstr(mock_stderr, "\n") != NULL);

    fprintf(stdout, "PASS: bc_error\n");
}

void test_bc_warn() {
    reset_mocks();

    bc_warn("test warn %d", 456);

    assert(strstr(mock_stderr, "bc: warning: ") != NULL);
    assert(strstr(mock_stderr, "test warn 456") != NULL);
    assert(strstr(mock_stderr, "\n") != NULL);

    fprintf(stdout, "PASS: bc_warn\n");
}

void test_bc_print() {
    bc_num *n;

    // Test null
    reset_mocks();
    bc_print(NULL);
    assert(strcmp(mock_stdout, "(null)") == 0);

    // Test zero
    reset_mocks();
    n = bc_from_string("0", 10);
    bc_print(n);
    assert(strcmp(mock_stdout, "0") == 0);
    bc_free(n);

    // Test zero with scale
    reset_mocks();
    n = bc_from_string("0", 10);
    n->scale = 3;
    bc_print(n);
    assert(strcmp(mock_stdout, "0.000") == 0);
    bc_free(n);

    // Test integer
    reset_mocks();
    n = bc_from_string("12345", 10);
    bc_print(n);
    assert(strcmp(mock_stdout, "12345") == 0);
    bc_free(n);

    // Test negative integer
    reset_mocks();
    n = bc_from_string("-9876", 10);
    bc_print(n);
    assert(strcmp(mock_stdout, "-9876") == 0);
    bc_free(n);

    // Test float (D > scale)
    reset_mocks();
    n = bc_from_string("12.345", 10);
    bc_print(n);
    assert(strcmp(mock_stdout, "12.345") == 0);
    bc_free(n);

    // Test small float (scale >= D)
    reset_mocks();
    n = bc_from_string("0.00123", 10);
    bc_print(n);
    assert(strcmp(mock_stdout, ".00123") == 0);
    bc_free(n);

    // Test negative float
    reset_mocks();
    n = bc_from_string("-0.045", 10);
    bc_print(n);
    assert(strcmp(mock_stdout, "-.045") == 0);
    bc_free(n);

    fprintf(stdout, "PASS: bc_print\n");
}

int main() {
    test_bc_error();
    test_bc_warn();
    test_bc_print();
    return 0;
}
