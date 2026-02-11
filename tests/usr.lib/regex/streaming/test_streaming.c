#include <string.h>
#include <regex.h>
#include "../test_common.h"

int test_streaming_basic(void) {
    regex_err_t err;
    regex_t *re = regex_compile("[a-z]+", REGEX_FLAG_EXTENDED, &err);
    regex_iter_t *it;
    size_t start, end;
    size_t caps[2];
    size_t cap_count;
    int got = 0;

    TEST_ASSERT(re != NULL);
    it = regex_iter_create(re, REGEX_ITER_DEFAULT, &err);
    TEST_ASSERT(it != NULL);

    TEST_ASSERT(regex_iter_feed(it, "hello ", 6) == REGEX_OK);
    TEST_ASSERT(regex_iter_feed(it, "world", 5) == REGEX_OK);
    TEST_ASSERT(regex_iter_finish(it) == REGEX_OK);

    while (regex_iter_next(it, &start, &end, caps, 2, &cap_count) > 0) {
        if (start == 0 && end == 5) {
            got = 1;
            break;
        }
    }
    TEST_ASSERT(got == 1);

    regex_iter_destroy(it);
    regex_free(re);
    return 0;
}
