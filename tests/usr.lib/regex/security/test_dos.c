#include <string.h>
#include <regex.h>
#include "../test_common.h"

int test_dos_limits(void) {
    regex_err_t err;
    regex_limits_t limits = regex_default_limits();
    char input[1024];
    regex_t *re;

    memset(input, 'a', sizeof(input));
    input[sizeof(input) - 1] = '\0';

    re = regex_compile("(a+)+b", REGEX_FLAG_EXTENDED, &err);
    TEST_ASSERT(re != NULL);

    limits.match_steps = 1000;
    TEST_ASSERT(regex_set_limits(re, &limits) == REGEX_OK);

    TEST_ASSERT(regex_match(re, input, strlen(input), NULL, 0, &err) < 0);
    TEST_ASSERT(err == REGEX_ERR_MATCH_TIMEOUT || err == REGEX_OK);

    regex_free(re);
    return 0;
}
