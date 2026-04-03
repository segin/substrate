#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdarg.h>
#include "num.h"

char mock_stderr[1024];
int mock_stderr_pos = 0;

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

#define fprintf mock_fprintf
#define vfprintf mock_vfprintf

#include "../../../usr.lib/bc/num.c"

#undef fprintf
#undef vfprintf

void reset_mocks() {
    mock_stderr_pos = 0;
    mock_stderr[0] = '\0';
}

void test_bc_error() {
    reset_mocks();

    bc_error("test error %d", 123);

    assert(strstr(mock_stderr, "bc: runtime error: ") != NULL);
    assert(strstr(mock_stderr, "test error 123") != NULL);
    assert(strstr(mock_stderr, "\n") != NULL);

    printf("PASS: bc_error\n");
}

void test_bc_warn() {
    reset_mocks();

    bc_warn("test warn %d", 456);

    assert(strstr(mock_stderr, "bc: warning: ") != NULL);
    assert(strstr(mock_stderr, "test warn 456") != NULL);
    assert(strstr(mock_stderr, "\n") != NULL);

    printf("PASS: bc_warn\n");
}

int main() {
    test_bc_error();
    test_bc_warn();
    return 0;
}
