#include <string.h>
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
