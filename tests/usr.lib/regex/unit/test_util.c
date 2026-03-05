#include <string.h>
#include <stdint.h>
#include "../test_common.h"

/* Declarations for internal utility functions to be tested */
uint32_t regex_ascii_tolower(uint32_t cp);
uint32_t regex_ascii_toupper(uint32_t cp);
uint32_t regex_unicode_tolower(uint32_t cp);
uint32_t regex_unicode_toupper(uint32_t cp);

int test_unicode_tolower(void) {
    /* Test standard ASCII range */
    TEST_ASSERT(regex_unicode_tolower('A') == 'a');
    TEST_ASSERT(regex_unicode_tolower('Z') == 'z');
    TEST_ASSERT(regex_unicode_tolower('a') == 'a');
    TEST_ASSERT(regex_unicode_tolower('z') == 'z');
    TEST_ASSERT(regex_unicode_tolower('0') == '0');
    TEST_ASSERT(regex_unicode_tolower('9') == '9');
    TEST_ASSERT(regex_unicode_tolower('!') == '!');
    TEST_ASSERT(regex_unicode_tolower(' ') == ' ');

#ifdef REGEX_USE_ICU
    /* Test some basic Unicode characters if ICU is enabled */
    /* U+00C4 LATIN CAPITAL LETTER A WITH DIAERESIS -> U+00E4 LATIN SMALL LETTER A WITH DIAERESIS */
    TEST_ASSERT(regex_unicode_tolower(0x00C4) == 0x00E4);
    /* U+03A9 GREEK CAPITAL LETTER OMEGA -> U+03C9 GREEK SMALL LETTER OMEGA */
    TEST_ASSERT(regex_unicode_tolower(0x03A9) == 0x03C9);
#else
    /* If ICU is not enabled, it should fallback to ASCII tolower, so non-ASCII remains unchanged */
    TEST_ASSERT(regex_unicode_tolower(0x00C4) == 0x00C4);
    TEST_ASSERT(regex_unicode_tolower(0x03A9) == 0x03A9);
#endif

    return 0;
}

int test_unicode_toupper(void) {
    /* Test standard ASCII range */
    TEST_ASSERT(regex_unicode_toupper('a') == 'A');
    TEST_ASSERT(regex_unicode_toupper('z') == 'Z');
    TEST_ASSERT(regex_unicode_toupper('A') == 'A');
    TEST_ASSERT(regex_unicode_toupper('Z') == 'Z');
    TEST_ASSERT(regex_unicode_toupper('0') == '0');
    TEST_ASSERT(regex_unicode_toupper('9') == '9');
    TEST_ASSERT(regex_unicode_toupper('!') == '!');
    TEST_ASSERT(regex_unicode_toupper(' ') == ' ');

#ifdef REGEX_USE_ICU
    /* Test some basic Unicode characters if ICU is enabled */
    /* U+00E4 LATIN SMALL LETTER A WITH DIAERESIS -> U+00C4 LATIN CAPITAL LETTER A WITH DIAERESIS */
    TEST_ASSERT(regex_unicode_toupper(0x00E4) == 0x00C4);
    /* U+03C9 GREEK SMALL LETTER OMEGA -> U+03A9 GREEK CAPITAL LETTER OMEGA */
    TEST_ASSERT(regex_unicode_toupper(0x03C9) == 0x03A9);
#else
    /* If ICU is not enabled, it should fallback to ASCII toupper, so non-ASCII remains unchanged */
    TEST_ASSERT(regex_unicode_toupper(0x00E4) == 0x00E4);
    TEST_ASSERT(regex_unicode_toupper(0x03C9) == 0x03C9);
#endif

    return 0;
}
