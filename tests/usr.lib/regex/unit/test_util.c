#include <string.h>
#include <stdint.h>
#include <regex.h>
#include "../../../../usr.lib/regex/src/regex_internal.h"
#include "../test_common.h"

int test_util_is_newline(void) {
    TEST_ASSERT(regex_is_newline('\n') != 0);
    TEST_ASSERT(regex_is_newline('\r') != 0);
    TEST_ASSERT(regex_is_newline(' ') == 0);
    TEST_ASSERT(regex_is_newline('\t') == 0);
    TEST_ASSERT(regex_is_newline('\v') == 0);
    TEST_ASSERT(regex_is_newline('\f') == 0);
    TEST_ASSERT(regex_is_newline('A') == 0);
    TEST_ASSERT(regex_is_newline('z') == 0);
    TEST_ASSERT(regex_is_newline('0') == 0);
    TEST_ASSERT(regex_is_newline(0) == 0);
    TEST_ASSERT(regex_is_newline(0xFFFD) == 0);
    TEST_ASSERT(regex_is_newline(0x10FFFF) == 0);
    return 0;
}

int test_ascii_toupper(void) {
    TEST_ASSERT(regex_ascii_toupper('a') == 'A');
    TEST_ASSERT(regex_ascii_toupper('z') == 'Z');
    TEST_ASSERT(regex_ascii_toupper('m') == 'M');
    TEST_ASSERT(regex_ascii_toupper('A') == 'A');
    TEST_ASSERT(regex_ascii_toupper('Z') == 'Z');
    TEST_ASSERT(regex_ascii_toupper('0') == '0');
    TEST_ASSERT(regex_ascii_toupper('9') == '9');
    TEST_ASSERT(regex_ascii_toupper('!') == '!');
    TEST_ASSERT(regex_ascii_toupper('~') == '~');
    TEST_ASSERT(regex_ascii_toupper(' ') == ' ');
    TEST_ASSERT(regex_ascii_toupper('\n') == '\n');
    TEST_ASSERT(regex_ascii_toupper('\0') == '\0');
    TEST_ASSERT(regex_ascii_toupper(0x1F4A9) == 0x1F4A9);
    return 0;
}

int test_util_toupper(void) {
    TEST_ASSERT(regex_ascii_toupper('a') == 'A');
    TEST_ASSERT(regex_ascii_toupper('z') == 'Z');
    TEST_ASSERT(regex_ascii_toupper('m') == 'M');
    TEST_ASSERT(regex_ascii_toupper('A') == 'A');
    TEST_ASSERT(regex_ascii_toupper('Z') == 'Z');
    TEST_ASSERT(regex_ascii_toupper('0') == '0');
    TEST_ASSERT(regex_ascii_toupper('_') == '_');

    TEST_ASSERT(regex_unicode_toupper('a') == 'A');
    TEST_ASSERT(regex_unicode_toupper('z') == 'Z');
    TEST_ASSERT(regex_unicode_toupper('A') == 'A');
    TEST_ASSERT(regex_unicode_toupper('0') == '0');
    TEST_ASSERT(regex_unicode_toupper(0x00E9) == 0x00E9 || regex_unicode_toupper(0x00E9) == 0x00C9);
    return 0;
}

int test_util_tolower(void) {
    TEST_ASSERT(regex_ascii_tolower('A') == 'a');
    TEST_ASSERT(regex_ascii_tolower('Z') == 'z');
    TEST_ASSERT(regex_ascii_tolower('M') == 'm');
    TEST_ASSERT(regex_ascii_tolower('a') == 'a');
    TEST_ASSERT(regex_ascii_tolower('z') == 'z');
    TEST_ASSERT(regex_ascii_tolower('0') == '0');
    TEST_ASSERT(regex_ascii_tolower('_') == '_');

    TEST_ASSERT(regex_unicode_tolower('A') == 'a');
    TEST_ASSERT(regex_unicode_tolower('Z') == 'z');
    TEST_ASSERT(regex_unicode_tolower('a') == 'a');
    TEST_ASSERT(regex_unicode_tolower('0') == '0');
    TEST_ASSERT(regex_unicode_tolower(0x00C9) == 0x00C9 || regex_unicode_tolower(0x00C9) == 0x00E9);
    return 0;
}

int test_unicode_tolower(void) {
    TEST_ASSERT(regex_unicode_tolower('A') == 'a');
    TEST_ASSERT(regex_unicode_tolower('Z') == 'z');
    TEST_ASSERT(regex_unicode_tolower('a') == 'a');
    TEST_ASSERT(regex_unicode_tolower('z') == 'z');
    TEST_ASSERT(regex_unicode_tolower('0') == '0');
    TEST_ASSERT(regex_unicode_tolower('9') == '9');
    TEST_ASSERT(regex_unicode_tolower('!') == '!');
    TEST_ASSERT(regex_unicode_tolower(' ') == ' ');
#ifdef REGEX_USE_ICU
    TEST_ASSERT(regex_unicode_tolower(0x00C4) == 0x00E4);
    TEST_ASSERT(regex_unicode_tolower(0x03A9) == 0x03C9);
#else
    TEST_ASSERT(regex_unicode_tolower(0x00C4) == 0x00C4);
    TEST_ASSERT(regex_unicode_tolower(0x03A9) == 0x03A9);
#endif
    return 0;
}

int test_unicode_toupper(void) {
    TEST_ASSERT(regex_unicode_toupper('a') == 'A');
    TEST_ASSERT(regex_unicode_toupper('z') == 'Z');
    TEST_ASSERT(regex_unicode_toupper('A') == 'A');
    TEST_ASSERT(regex_unicode_toupper('Z') == 'Z');
    TEST_ASSERT(regex_unicode_toupper('0') == '0');
    TEST_ASSERT(regex_unicode_toupper('9') == '9');
    TEST_ASSERT(regex_unicode_toupper('!') == '!');
    TEST_ASSERT(regex_unicode_toupper(' ') == ' ');
#ifdef REGEX_USE_ICU
    TEST_ASSERT(regex_unicode_toupper(0x00E4) == 0x00C4);
    TEST_ASSERT(regex_unicode_toupper(0x03C9) == 0x03A9);
#else
    TEST_ASSERT(regex_unicode_toupper(0x00E4) == 0x00E4);
    TEST_ASSERT(regex_unicode_toupper(0x03C9) == 0x03C9);
#endif
    return 0;
}
