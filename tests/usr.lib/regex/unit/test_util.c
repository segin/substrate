#include <string.h>
#include <regex.h>
#include "../../../../usr.lib/regex/src/regex_internal.h"
#include "../test_common.h"

int test_util_unicode_case(void) {
    /* Test regex_unicode_tolower */
    TEST_ASSERT(regex_unicode_tolower('A') == 'a');
    TEST_ASSERT(regex_unicode_tolower('Z') == 'z');
    TEST_ASSERT(regex_unicode_tolower('a') == 'a');
    TEST_ASSERT(regex_unicode_tolower('z') == 'z');
    TEST_ASSERT(regex_unicode_tolower('1') == '1');
#ifdef REGEX_USE_ICU
    TEST_ASSERT(regex_unicode_tolower(0x00C9) == 0x00E9); /* U+00C9 (E with acute) -> U+00E9 (e with acute) */
#else
    TEST_ASSERT(regex_unicode_tolower(0x00C9) == 0x00C9); /* fallback to ASCII without ICU */
#endif

    /* Test regex_unicode_toupper */
    TEST_ASSERT(regex_unicode_toupper('a') == 'A');
    TEST_ASSERT(regex_unicode_toupper('z') == 'Z');
    TEST_ASSERT(regex_unicode_toupper('A') == 'A');
    TEST_ASSERT(regex_unicode_toupper('Z') == 'Z');
    TEST_ASSERT(regex_unicode_toupper('1') == '1');
#ifdef REGEX_USE_ICU
    TEST_ASSERT(regex_unicode_toupper(0x00E9) == 0x00C9); /* U+00E9 (e with acute) -> U+00C9 (E with acute) */
#else
    TEST_ASSERT(regex_unicode_toupper(0x00E9) == 0x00E9); /* fallback to ASCII without ICU */
#endif

    return 0;
}
