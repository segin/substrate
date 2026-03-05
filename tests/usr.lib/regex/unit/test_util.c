#include "../test_common.h"
#include "../../../../usr.lib/regex/src/regex_internal.h"

int test_util_ascii_tolower(void) {
    // 1. Standard lowercase letters
    for (uint32_t cp = 'a'; cp <= 'z'; cp++) {
        TEST_ASSERT(regex_ascii_tolower(cp) == cp);
    }

    // 2. Standard uppercase letters
    for (uint32_t cp = 'A'; cp <= 'Z'; cp++) {
        TEST_ASSERT(regex_ascii_tolower(cp) == cp - 'A' + 'a');
    }

    // 3. Digits
    for (uint32_t cp = '0'; cp <= '9'; cp++) {
        TEST_ASSERT(regex_ascii_tolower(cp) == cp);
    }

    // 4. Boundary and punctuation characters
    TEST_ASSERT(regex_ascii_tolower('@') == '@'); // Before 'A'
    TEST_ASSERT(regex_ascii_tolower('[') == '['); // After 'Z'
    TEST_ASSERT(regex_ascii_tolower('`') == '`'); // Before 'a'
    TEST_ASSERT(regex_ascii_tolower('{') == '{'); // After 'z'

    // 5. Out of bounds and edge values
    TEST_ASSERT(regex_ascii_tolower(0) == 0);
    TEST_ASSERT(regex_ascii_tolower(127) == 127);
    TEST_ASSERT(regex_ascii_tolower(128) == 128);
    TEST_ASSERT(regex_ascii_tolower(255) == 255);
    TEST_ASSERT(regex_ascii_tolower(REGEX_MAX_CODEPOINT) == REGEX_MAX_CODEPOINT);

    return 0;
}
