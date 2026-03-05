#include <stdint.h>
#include <string.h>
#include <regex.h>
#include "../test_common.h"
#include "../../../../usr.lib/regex/src/regex_internal.h"

extern size_t regex_utf8_encode(uint32_t cp, char out[4]);

int test_utf8_encode(void) {
    char out[4];
    size_t len;

    /* 1-byte */
    memset(out, 0, sizeof(out));
    len = regex_utf8_encode(0x24, out); /* '$' */
    TEST_ASSERT(len == 1);
    TEST_ASSERT(out[0] == '\x24');

    /* 2-byte */
    memset(out, 0, sizeof(out));
    len = regex_utf8_encode(0xA2, out); /* '¢' */
    TEST_ASSERT(len == 2);
    TEST_ASSERT(out[0] == '\xC2');
    TEST_ASSERT(out[1] == '\xA2');

    /* 3-byte */
    memset(out, 0, sizeof(out));
    len = regex_utf8_encode(0x20AC, out); /* '€' */
    TEST_ASSERT(len == 3);
    TEST_ASSERT(out[0] == '\xE2');
    TEST_ASSERT(out[1] == '\x82');
    TEST_ASSERT(out[2] == '\xAC');

    /* 4-byte */
    memset(out, 0, sizeof(out));
    len = regex_utf8_encode(0x10348, out); /* '𐍈' */
    TEST_ASSERT(len == 4);
    TEST_ASSERT(out[0] == '\xF0');
    TEST_ASSERT(out[1] == '\x90');
    TEST_ASSERT(out[2] == '\x8D');
    TEST_ASSERT(out[3] == '\x88');

    /* Edge cases */
    /* Max 1-byte */
    len = regex_utf8_encode(0x7F, out);
    TEST_ASSERT(len == 1);
    TEST_ASSERT(out[0] == '\x7F');

    /* Max 2-byte */
    len = regex_utf8_encode(0x7FF, out);
    TEST_ASSERT(len == 2);
    TEST_ASSERT(out[0] == '\xDF');
    TEST_ASSERT(out[1] == '\xBF');

    /* Max 3-byte */
    len = regex_utf8_encode(0xFFFF, out);
    TEST_ASSERT(len == 3);
    TEST_ASSERT(out[0] == '\xEF');
    TEST_ASSERT(out[1] == '\xBF');
    TEST_ASSERT(out[2] == '\xBF');

    /* Max 4-byte (or max Unicode) */
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
    const char *pattern = "\xE2\x9C\x93"; /* checkmark */
    const char *text = "ok \xE2\x9C\x93";
    size_t caps[2];
    regex_t *re = regex_compile(pattern, REGEX_FLAG_UTF8 | REGEX_FLAG_LITERAL, &err);
    TEST_ASSERT(re != NULL);
    TEST_ASSERT(regex_match(re, text, strlen(text), caps, 2, &err) >= 0);
    regex_free(re);
    return 0;
}

int test_unicode_case(void) {
    /* ASCII conversions */
    TEST_ASSERT(regex_unicode_tolower('A') == 'a');
    TEST_ASSERT(regex_unicode_toupper('a') == 'A');
    TEST_ASSERT(regex_unicode_tolower('Z') == 'z');
    TEST_ASSERT(regex_unicode_toupper('z') == 'Z');

    /* Non-letters */
    TEST_ASSERT(regex_unicode_tolower('1') == '1');
    TEST_ASSERT(regex_unicode_toupper('1') == '1');
    TEST_ASSERT(regex_unicode_tolower('-') == '-');
    TEST_ASSERT(regex_unicode_toupper('-') == '-');
    TEST_ASSERT(regex_unicode_tolower(0x00) == 0x00);
    TEST_ASSERT(regex_unicode_toupper(0x00) == 0x00);

    /* If REGEX_USE_ICU is defined, it would actually convert unicode case.
       If not, it just falls back to ASCII. Let's test a non-ASCII code point.
       Just ensure it doesn't crash and behaves predictably. */
#ifdef REGEX_USE_ICU
    /* E.g. Cyrillic Capital Letter A (U+0410) -> Cyrillic Small Letter A (U+0430) */
    TEST_ASSERT(regex_unicode_tolower(0x0410) == 0x0430);
    TEST_ASSERT(regex_unicode_toupper(0x0430) == 0x0410);
#else
    /* Fallback behaviour just returns the codepoint */
    TEST_ASSERT(regex_unicode_tolower(0x0410) == 0x0410);
    TEST_ASSERT(regex_unicode_toupper(0x0430) == 0x0430);
#endif

    return 0;
}
