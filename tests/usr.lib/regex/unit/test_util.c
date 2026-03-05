#include <string.h>
#include <regex.h>
#include "../../../../usr.lib/regex/src/regex_internal.h"
#include "../test_common.h"

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
