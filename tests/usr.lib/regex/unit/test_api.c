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

int test_api_split_free(void) {
    regex_split_result_t out = {0};
    out.count = 2;
    out.items = malloc(2 * sizeof(char *));
    TEST_ASSERT(out.items != NULL);
    out.items[0] = strdup("hello");
    out.items[1] = strdup("world");
    TEST_ASSERT(out.items[0] != NULL);
    TEST_ASSERT(out.items[1] != NULL);

    /* Free the valid split result */
    regex_split_free(&out);
    TEST_ASSERT(out.items == NULL);
    TEST_ASSERT(out.count == 0);

    /* Test freeing NULL */
    regex_split_free(NULL);

    /* Test freeing empty result */
    regex_split_result_t empty = {0};
    regex_split_free(&empty);

    return 0;
}
