#include <string.h>
#include <regex.h>
#include "../test_common.h"

int test_replace_basic(void) {
    regex_err_t err;
    char *out = NULL;
    size_t out_len = 0;
    regex_t *re = regex_compile("[0-9]+", REGEX_FLAG_EXTENDED, &err);
    TEST_ASSERT(re != NULL);
    TEST_ASSERT(regex_replace(re, "id=42", 5, "num:$0", 0, &out, &out_len) == REGEX_OK);
    TEST_ASSERT(strcmp(out, "id=num:42") == 0);
    free(out);
    regex_free(re);
    return 0;
}
