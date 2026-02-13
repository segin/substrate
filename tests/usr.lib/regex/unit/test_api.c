#include <string.h>
#include <regex.h>
#include "../test_common.h"

int test_api_basic(void) {
    regex_err_t err;
    regex_t *re = regex_compile("abc", REGEX_FLAG_LITERAL, &err);
    TEST_ASSERT(re != NULL);
    TEST_ASSERT(regex_capture_count(re) == 1);
    regex_free(re);
    return 0;
}

int test_api_match(void) {
    regex_err_t err;
    size_t caps[2];
    regex_t *re = regex_compile("[a-z]+", REGEX_FLAG_EXTENDED, &err);
    TEST_ASSERT(re != NULL);
    TEST_ASSERT(regex_match(re, "hello", 5, caps, 2, &err) >= 0);
    TEST_ASSERT(caps[0] == 0 && caps[1] == 5);
    regex_free(re);
    return 0;
}
