#ifndef TEST_REGEX_COMMON_H
#define TEST_REGEX_COMMON_H

#include <stdio.h>
#include <stdlib.h>

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "TEST FAILED: %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        return 1; \
    } \
} while (0)

#endif
