#include <string.h>
#include <regex.h>
#include "../test_common.h"
#include "../../../../usr.lib/regex/src/regex_internal.h"

int test_util_is_newline(void) {
    /* Happy path: true newlines */
    TEST_ASSERT(regex_is_newline('\n') != 0);
    TEST_ASSERT(regex_is_newline('\r') != 0);

    /* Negative cases: other whitespace */
    TEST_ASSERT(regex_is_newline(' ') == 0);
    TEST_ASSERT(regex_is_newline('\t') == 0);
    TEST_ASSERT(regex_is_newline('\v') == 0);
    TEST_ASSERT(regex_is_newline('\f') == 0);

    /* Negative cases: normal characters */
    TEST_ASSERT(regex_is_newline('A') == 0);
    TEST_ASSERT(regex_is_newline('z') == 0);
    TEST_ASSERT(regex_is_newline('0') == 0);

    /* Edge cases: null and non-ascii */
    TEST_ASSERT(regex_is_newline(0) == 0);
    TEST_ASSERT(regex_is_newline(0xFFFD) == 0);
    TEST_ASSERT(regex_is_newline(0x10FFFF) == 0);

    return 0;
}
