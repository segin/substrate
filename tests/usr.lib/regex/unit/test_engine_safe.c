#include <string.h>
#include "../../../../usr.lib/regex/src/regex_internal.h"
#include "../test_common.h"

int test_engine_safe_init(void) {
    regex_t re;
    regex_err_t err;

    // The safe engine init simply returns REGEX_OK and ignores its arguments.
    // Let's ensure it handles valid inputs and NULL inputs properly without crashing.

    // Test 1: valid arguments
    memset(&re, 0, sizeof(re));
    err = regex_engine_safe_init(&re, "test_pattern", REGEX_FLAG_EXTENDED);
    TEST_ASSERT(err == REGEX_OK);

    // Test 2: NULL arguments (where applicable)
    err = regex_engine_safe_init(NULL, NULL, 0);
    TEST_ASSERT(err == REGEX_OK);

    return 0;
}
