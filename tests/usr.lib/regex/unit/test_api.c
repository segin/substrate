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

int test_api_split_free(void) {
    /* Test NULL */
    regex_split_free(NULL);

    /* Test empty */
    regex_split_result_t empty = {0};
    regex_split_free(&empty);
    TEST_ASSERT(empty.items == NULL);
    TEST_ASSERT(empty.count == 0);

    /* Test with items */
    regex_split_result_t res = {0};
    res.count = 2;
    res.items = malloc(2 * sizeof(char *));
    TEST_ASSERT(res.items != NULL);
    res.items[0] = strdup("hello");
    res.items[1] = strdup("world");
    TEST_ASSERT(res.items[0] != NULL);
    TEST_ASSERT(res.items[1] != NULL);

    regex_split_free(&res);
    TEST_ASSERT(res.items == NULL);
    TEST_ASSERT(res.count == 0);

    /* Test items allocated but count 0 */
    regex_split_result_t res2 = {0};
    res2.count = 0;
    res2.items = malloc(1 * sizeof(char *));
    TEST_ASSERT(res2.items != NULL);
    res2.items[0] = NULL;

    regex_split_free(&res2);
    TEST_ASSERT(res2.items == NULL);
    TEST_ASSERT(res2.count == 0);

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
