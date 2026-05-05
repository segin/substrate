#ifndef TEST_LINK_COMMON_H
#define TEST_LINK_COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "TEST FAILED: %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        return 1; \
    } \
} while (0)

#define TEST_ASSERT_STR(actual, expected) do { \
    const char *_actual = (actual); \
    const char *_expected = (expected); \
    if ((_actual == NULL && _expected != NULL) || \
        (_actual != NULL && _expected == NULL) || \
        (_actual != NULL && _expected != NULL && strcmp(_actual, _expected) != 0)) { \
        fprintf(stderr, "TEST FAILED: %s:%d: expected \"%s\", got \"%s\"\n", \
                __FILE__, __LINE__, _expected ? _expected : "NULL", _actual ? _actual : "NULL"); \
        return 1; \
    } \
} while (0)

#endif
