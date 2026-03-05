#include <stdint.h>
#include <string.h>
#include <regex.h>
#include "../test_common.h"
#include "../../../../usr.lib/regex/src/regex_internal.h"

extern size_t regex_utf8_encode(uint32_t cp, char out[4]);

int test_utf8_encode(void) {
    char out[4];
    size_t len;

    memset(out, 0, sizeof(out));
    len = regex_utf8_encode(0x24, out);
    TEST_ASSERT(len == 1);
    TEST_ASSERT(out[0] == '\x24');

    memset(out, 0, sizeof(out));
    len = regex_utf8_encode(0xA2, out);
    TEST_ASSERT(len == 2);
    TEST_ASSERT(out[0] == '\xC2');
    TEST_ASSERT(out[1] == '\xA2');

    memset(out, 0, sizeof(out));
    len = regex_utf8_encode(0x20AC, out);
    TEST_ASSERT(len == 3);
    TEST_ASSERT(out[0] == '\xE2');
    TEST_ASSERT(out[1] == '\x82');
    TEST_ASSERT(out[2] == '\xAC');

    memset(out, 0, sizeof(out));
    len = regex_utf8_encode(0x10348, out);
    TEST_ASSERT(len == 4);
    TEST_ASSERT(out[0] == '\xF0');
    TEST_ASSERT(out[1] == '\x90');
    TEST_ASSERT(out[2] == '\x8D');
    TEST_ASSERT(out[3] == '\x88');

    len = regex_utf8_encode(0x7F, out);
    TEST_ASSERT(len == 1);
    TEST_ASSERT(out[0] == '\x7F');

    len = regex_utf8_encode(0x7FF, out);
    TEST_ASSERT(len == 2);
    TEST_ASSERT(out[0] == '\xDF');
    TEST_ASSERT(out[1] == '\xBF');

    len = regex_utf8_encode(0xFFFF, out);
    TEST_ASSERT(len == 3);
    TEST_ASSERT(out[0] == '\xEF');
    TEST_ASSERT(out[1] == '\xBF');
    TEST_ASSERT(out[2] == '\xBF');

    len = regex_utf8_encode(0x10FFFF, out);
    TEST_ASSERT(len == 4);
    TEST_ASSERT(out[0] == '\xF4');
    TEST_ASSERT(out[1] == '\x8F');
    TEST_ASSERT(out[2] == '\xBF');
    TEST_ASSERT(out[3] == '\xBF');

    return 0;
}

int test_utf8_literal(void) {
    regex_err_t err;
    const char *pattern = "\xE2\x9C\x93";
    const char *text = "ok \xE2\x9C\x93";
    size_t caps[2];
    regex_t *re = regex_compile(pattern, REGEX_FLAG_UTF8 | REGEX_FLAG_LITERAL, &err);
    TEST_ASSERT(re != NULL);
    TEST_ASSERT(regex_match(re, text, strlen(text), caps, 2, &err) >= 0);
    regex_free(re);
    return 0;
}

int test_utf8_decode_valid(void) {
    size_t index = 0;
    uint32_t cp = 0;
    const char *s1 = "A";
    TEST_ASSERT(regex_utf8_decode(s1, 1, &index, &cp) == 1);
    TEST_ASSERT(cp == 'A');
    TEST_ASSERT(index == 1);

    const char *s2 = "\xC3\xB1";
    index = 0;
    TEST_ASSERT(regex_utf8_decode(s2, 2, &index, &cp) == 1);
    TEST_ASSERT(cp == 0x00F1);
    TEST_ASSERT(index == 2);

    const char *s3 = "\xE2\x9C\x93";
    index = 0;
    TEST_ASSERT(regex_utf8_decode(s3, 3, &index, &cp) == 1);
    TEST_ASSERT(cp == 0x2713);
    TEST_ASSERT(index == 3);

    const char *s4 = "\xF0\x9F\x98\x8A";
    index = 0;
    TEST_ASSERT(regex_utf8_decode(s4, 4, &index, &cp) == 1);
    TEST_ASSERT(cp == 0x1F60A);
    TEST_ASSERT(index == 4);

    return 0;
}

int test_utf8_decode_invalid(void) {
    size_t index = 0;
    uint32_t cp = 0;

    TEST_ASSERT(regex_utf8_decode(NULL, 1, &index, &cp) == 0);
    TEST_ASSERT(regex_utf8_decode("A", 1, NULL, &cp) == 0);
    TEST_ASSERT(regex_utf8_decode("A", 1, &index, NULL) == 0);

    index = 1;
    TEST_ASSERT(regex_utf8_decode("A", 1, &index, &cp) == 0);

    index = 0;
    TEST_ASSERT(regex_utf8_decode("\xC3", 1, &index, &cp) == 0);
    index = 0;
    TEST_ASSERT(regex_utf8_decode("\xC3\x28", 2, &index, &cp) == 0);
    index = 0;
    TEST_ASSERT(regex_utf8_decode("\xC1\x81", 2, &index, &cp) == 0);

    index = 0;
    TEST_ASSERT(regex_utf8_decode("\xE2\x9C", 2, &index, &cp) == 0);
    index = 0;
    TEST_ASSERT(regex_utf8_decode("\xE2\x28\x93", 3, &index, &cp) == 0);
    index = 0;
    TEST_ASSERT(regex_utf8_decode("\xE2\x9C\x28", 3, &index, &cp) == 0);
    index = 0;
    TEST_ASSERT(regex_utf8_decode("\xE0\x83\xB1", 3, &index, &cp) == 0);

    index = 0;
    TEST_ASSERT(regex_utf8_decode("\xF0\x9F\x98", 3, &index, &cp) == 0);
    index = 0;
    TEST_ASSERT(regex_utf8_decode("\xF0\x28\x98\x8A", 4, &index, &cp) == 0);
    index = 0;
    TEST_ASSERT(regex_utf8_decode("\xF0\x9F\x28\x8A", 4, &index, &cp) == 0);
    index = 0;
    TEST_ASSERT(regex_utf8_decode("\xF0\x9F\x98\x28", 4, &index, &cp) == 0);
    index = 0;
    TEST_ASSERT(regex_utf8_decode("\xF0\x82\x9C\x93", 4, &index, &cp) == 0);

    index = 0;
    TEST_ASSERT(regex_utf8_decode("\xFF", 1, &index, &cp) == 0);
    index = 0;
    TEST_ASSERT(regex_utf8_decode("\xF8\x88\x80\x80\x80", 5, &index, &cp) == 0);
    index = 0;
    TEST_ASSERT(regex_utf8_decode("\xF4\x90\x80\x80", 4, &index, &cp) == 0);

    return 0;
}

int test_unicode_case(void) {
    TEST_ASSERT(regex_unicode_tolower('A') == 'a');
    TEST_ASSERT(regex_unicode_toupper('a') == 'A');
    TEST_ASSERT(regex_unicode_tolower('Z') == 'z');
    TEST_ASSERT(regex_unicode_toupper('z') == 'Z');
    TEST_ASSERT(regex_unicode_tolower('1') == '1');
    TEST_ASSERT(regex_unicode_toupper('1') == '1');
    TEST_ASSERT(regex_unicode_tolower('-') == '-');
    TEST_ASSERT(regex_unicode_toupper('-') == '-');
    TEST_ASSERT(regex_unicode_tolower(0x00) == 0x00);
    TEST_ASSERT(regex_unicode_toupper(0x00) == 0x00);
#ifdef REGEX_USE_ICU
    TEST_ASSERT(regex_unicode_tolower(0x0410) == 0x0430);
    TEST_ASSERT(regex_unicode_toupper(0x0430) == 0x0410);
#else
    TEST_ASSERT(regex_unicode_tolower(0x0410) == 0x0410);
    TEST_ASSERT(regex_unicode_toupper(0x0430) == 0x0430);
#endif
    return 0;
}
