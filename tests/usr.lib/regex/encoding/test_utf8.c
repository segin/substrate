#include <string.h>
#include <regex.h>
#include "../test_common.h"

int test_utf8_literal(void) {
    regex_err_t err;
    const char *pattern = "\xE2\x9C\x93"; /* checkmark */
    const char *text = "ok \xE2\x9C\x93";
    size_t caps[2];
    regex_t *re = regex_compile(pattern, REGEX_FLAG_UTF8 | REGEX_FLAG_LITERAL, &err);
    TEST_ASSERT(re != NULL);
    TEST_ASSERT(regex_match(re, text, strlen(text), caps, 2, &err) >= 0);
    regex_free(re);
    return 0;
}
