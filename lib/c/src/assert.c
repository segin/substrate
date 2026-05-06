#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

void __assert_fail(const char *expr, const char *file, unsigned int line, const char *func) {
    fprintf(stderr, "Assertion failed: %s (%s: %s: %u)\n", expr, file, func, line);
    abort();
}

