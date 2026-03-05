#include <string.h>
#include <regex.h>
#include "../test_common.h"
#include "../../../../usr.lib/regex/src/regex_internal.h"

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

int test_utf8_decode_valid(void) {
    size_t index = 0;
    uint32_t cp = 0;
    const char *s1 = "A"; // 1 byte
    TEST_ASSERT(regex_utf8_decode(s1, 1, &index, &cp) == 1);
    TEST_ASSERT(cp == 'A');
    TEST_ASSERT(index == 1);

    const char *s2 = "\xC3\xB1"; // 2 bytes (ñ)
    index = 0;
    TEST_ASSERT(regex_utf8_decode(s2, 2, &index, &cp) == 1);
    TEST_ASSERT(cp == 0x00F1);
    TEST_ASSERT(index == 2);

    const char *s3 = "\xE2\x9C\x93"; // 3 bytes (✓)
    index = 0;
    TEST_ASSERT(regex_utf8_decode(s3, 3, &index, &cp) == 1);
    TEST_ASSERT(cp == 0x2713);
    TEST_ASSERT(index == 3);

    const char *s4 = "\xF0\x9F\x98\x8A"; // 4 bytes (😊)
    index = 0;
    TEST_ASSERT(regex_utf8_decode(s4, 4, &index, &cp) == 1);
    TEST_ASSERT(cp == 0x1F60A);
    TEST_ASSERT(index == 4);

    return 0;
}

int test_utf8_decode_invalid(void) {
    size_t index = 0;
    uint32_t cp = 0;

    // NULL arguments
    TEST_ASSERT(regex_utf8_decode(NULL, 1, &index, &cp) == 0);
    TEST_ASSERT(regex_utf8_decode("A", 1, NULL, &cp) == 0);
    TEST_ASSERT(regex_utf8_decode("A", 1, &index, NULL) == 0);

    // Index out of bounds
    index = 1;
    TEST_ASSERT(regex_utf8_decode("A", 1, &index, &cp) == 0);

    // Truncated 2-byte sequence
    index = 0;
    TEST_ASSERT(regex_utf8_decode("\xC3", 1, &index, &cp) == 0);

    // Invalid continuation byte for 2-byte sequence
    index = 0;
    TEST_ASSERT(regex_utf8_decode("\xC3\x28", 2, &index, &cp) == 0);

    // Overlong 2-byte encoding of ASCII 'A' (C1 81)
    index = 0;
    TEST_ASSERT(regex_utf8_decode("\xC1\x81", 2, &index, &cp) == 0);

    // Truncated 3-byte sequence
    index = 0;
    TEST_ASSERT(regex_utf8_decode("\xE2\x9C", 2, &index, &cp) == 0);

    // Invalid continuation byte 1 for 3-byte sequence
    index = 0;
    TEST_ASSERT(regex_utf8_decode("\xE2\x28\x93", 3, &index, &cp) == 0);

    // Invalid continuation byte 2 for 3-byte sequence
    index = 0;
    TEST_ASSERT(regex_utf8_decode("\xE2\x9C\x28", 3, &index, &cp) == 0);

    // Overlong 3-byte encoding of 2-byte sequence
    index = 0;
    TEST_ASSERT(regex_utf8_decode("\xE0\x83\xB1", 3, &index, &cp) == 0);

    // Truncated 4-byte sequence
    index = 0;
    TEST_ASSERT(regex_utf8_decode("\xF0\x9F\x98", 3, &index, &cp) == 0);

    // Invalid continuation byte 1 for 4-byte sequence
    index = 0;
    TEST_ASSERT(regex_utf8_decode("\xF0\x28\x98\x8A", 4, &index, &cp) == 0);

    // Invalid continuation byte 2 for 4-byte sequence
    index = 0;
    TEST_ASSERT(regex_utf8_decode("\xF0\x9F\x28\x8A", 4, &index, &cp) == 0);

    // Invalid continuation byte 3 for 4-byte sequence
    index = 0;
    TEST_ASSERT(regex_utf8_decode("\xF0\x9F\x98\x28", 4, &index, &cp) == 0);

    // Overlong 4-byte encoding of 3-byte sequence
    index = 0;
    TEST_ASSERT(regex_utf8_decode("\xF0\x82\x9C\x93", 4, &index, &cp) == 0);

    // Invalid starting byte
    index = 0;
    TEST_ASSERT(regex_utf8_decode("\xFF", 1, &index, &cp) == 0);

    // Invalid starting byte for 5-byte sequence
    index = 0;
    TEST_ASSERT(regex_utf8_decode("\xF8\x88\x80\x80\x80", 5, &index, &cp) == 0);

    // Codepoint out of bounds (> 0x10FFFF)
    index = 0;
    // F4 90 80 80 encodes to 110000 which is > 10FFFF
    TEST_ASSERT(regex_utf8_decode("\xF4\x90\x80\x80", 4, &index, &cp) == 0);

    return 0;
}
