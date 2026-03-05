#include <string.h>
#include <regex.h>
#include "../../../../usr.lib/regex/src/regex_internal.h"
#include "../test_common.h"

int test_util_toupper(void) {
    /* Test ASCII behavior */
    TEST_ASSERT(regex_ascii_toupper('a') == 'A');
    TEST_ASSERT(regex_ascii_toupper('z') == 'Z');
    TEST_ASSERT(regex_ascii_toupper('m') == 'M');
    TEST_ASSERT(regex_ascii_toupper('A') == 'A');
    TEST_ASSERT(regex_ascii_toupper('Z') == 'Z');
    TEST_ASSERT(regex_ascii_toupper('0') == '0');
    TEST_ASSERT(regex_ascii_toupper('_') == '_');

    /* Test Unicode fallback/ICU behavior */
    TEST_ASSERT(regex_unicode_toupper('a') == 'A');
    TEST_ASSERT(regex_unicode_toupper('z') == 'Z');
    TEST_ASSERT(regex_unicode_toupper('A') == 'A');
    TEST_ASSERT(regex_unicode_toupper('0') == '0');

    /* Test characters outside ASCII */
    /* When ICU is disabled, unicode_toupper falls back to ascii_toupper,
     * which returns the codepoint unchanged for non-ASCII.
     * When ICU is enabled, it should correctly uppercase if applicable.
     * We can test a char that isn't modified by ascii to ensure it doesn't break. */
    TEST_ASSERT(regex_unicode_toupper(0x00E9) == 0x00E9 || regex_unicode_toupper(0x00E9) == 0x00C9); // é -> É (if ICU) or é (if no ICU)

    return 0;
}

int test_util_tolower(void) {
    /* Test ASCII behavior */
    TEST_ASSERT(regex_ascii_tolower('A') == 'a');
    TEST_ASSERT(regex_ascii_tolower('Z') == 'z');
    TEST_ASSERT(regex_ascii_tolower('M') == 'm');
    TEST_ASSERT(regex_ascii_tolower('a') == 'a');
    TEST_ASSERT(regex_ascii_tolower('z') == 'z');
    TEST_ASSERT(regex_ascii_tolower('0') == '0');
    TEST_ASSERT(regex_ascii_tolower('_') == '_');

    /* Test Unicode fallback/ICU behavior */
    TEST_ASSERT(regex_unicode_tolower('A') == 'a');
    TEST_ASSERT(regex_unicode_tolower('Z') == 'z');
    TEST_ASSERT(regex_unicode_tolower('a') == 'a');
    TEST_ASSERT(regex_unicode_tolower('0') == '0');

    /* Test characters outside ASCII */
    TEST_ASSERT(regex_unicode_tolower(0x00C9) == 0x00C9 || regex_unicode_tolower(0x00C9) == 0x00E9); // É -> é (if ICU) or É (if no ICU)

    return 0;
}
