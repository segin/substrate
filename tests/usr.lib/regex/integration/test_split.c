#include <string.h>
#include <regex.h>
#include "../test_common.h"

int test_split_basic(void) {
    regex_err_t err;
    regex_split_result_t out = {0};
    regex_t *re = regex_compile(",", REGEX_FLAG_EXTENDED, &err);
    TEST_ASSERT(re != NULL);

    TEST_ASSERT(regex_split(re, "a,b,c", 5, &out, 0) == REGEX_OK);
    TEST_ASSERT(out.count == 3);
    TEST_ASSERT(strcmp(out.items[0], "a") == 0);
    TEST_ASSERT(strcmp(out.items[1], "b") == 0);
    TEST_ASSERT(strcmp(out.items[2], "c") == 0);

    /* Free the valid split result */
    regex_split_free(&out);
    TEST_ASSERT(out.items == NULL);
    TEST_ASSERT(out.count == 0);

    /* Test freeing NULL */
    regex_split_free(NULL);

    /* Test freeing empty result */
    regex_split_result_t empty = {0};
    regex_split_free(&empty);

    regex_free(re);
    return 0;
}
