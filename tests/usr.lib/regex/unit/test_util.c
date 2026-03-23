#include <string.h>
#include <stdint.h>
#include <regex.h>
#include "../../../../usr.lib/regex/src/regex_internal.h"
#include "../test_common.h"

int test_util_ascii_tolower(void) {
    for (uint32_t cp = 'a'; cp <= 'z'; cp++) {
        TEST_ASSERT(regex_ascii_tolower(cp) == cp);
    }

    for (uint32_t cp = 'A'; cp <= 'Z'; cp++) {
        TEST_ASSERT(regex_ascii_tolower(cp) == cp - 'A' + 'a');
    }

    for (uint32_t cp = '0'; cp <= '9'; cp++) {
        TEST_ASSERT(regex_ascii_tolower(cp) == cp);
    }

    TEST_ASSERT(regex_ascii_tolower('@') == '@');
    TEST_ASSERT(regex_ascii_tolower('[') == '[');
    TEST_ASSERT(regex_ascii_tolower('`') == '`');
    TEST_ASSERT(regex_ascii_tolower('{') == '{');

    TEST_ASSERT(regex_ascii_tolower(0) == 0);
    TEST_ASSERT(regex_ascii_tolower(127) == 127);
    TEST_ASSERT(regex_ascii_tolower(128) == 128);
    TEST_ASSERT(regex_ascii_tolower(255) == 255);
    TEST_ASSERT(regex_ascii_tolower(REGEX_MAX_CODEPOINT) == REGEX_MAX_CODEPOINT);

    return 0;
}

int test_util_utf8_decode(void) {
    size_t index = 0;
    uint32_t cp = 0;

    // Valid 1-byte
    TEST_ASSERT(regex_utf8_decode("A", 1, &index, &cp) == 1);
    TEST_ASSERT(cp == 'A');
    TEST_ASSERT(index == 1);

    // Valid 2-byte
    index = 0;
    TEST_ASSERT(regex_utf8_decode("\xC3\xB1", 2, &index, &cp) == 1);
    TEST_ASSERT(cp == 0x00F1);
    TEST_ASSERT(index == 2);

    // Valid 3-byte
    index = 0;
    TEST_ASSERT(regex_utf8_decode("\xE2\x9C\x93", 3, &index, &cp) == 1);
    TEST_ASSERT(cp == 0x2713);
    TEST_ASSERT(index == 3);

    // Valid 4-byte
    index = 0;
    TEST_ASSERT(regex_utf8_decode("\xF0\x9F\x98\x8A", 4, &index, &cp) == 1);
    TEST_ASSERT(cp == 0x1F60A);
    TEST_ASSERT(index == 4);

    // Invalid parameters
    index = 0;
    TEST_ASSERT(regex_utf8_decode(NULL, 1, &index, &cp) == 0);
    TEST_ASSERT(regex_utf8_decode("A", 1, NULL, &cp) == 0);
    TEST_ASSERT(regex_utf8_decode("A", 1, &index, NULL) == 0);

    // Out of bounds initial index
    index = 1;
    TEST_ASSERT(regex_utf8_decode("A", 1, &index, &cp) == 0);

    // Truncated sequences
    index = 0;
    TEST_ASSERT(regex_utf8_decode("\xC3", 1, &index, &cp) == 0);
    index = 0;
    TEST_ASSERT(regex_utf8_decode("\xE2\x9C", 2, &index, &cp) == 0);
    index = 0;
    TEST_ASSERT(regex_utf8_decode("\xF0\x9F\x98", 3, &index, &cp) == 0);

    // Invalid continuations
    index = 0;
    TEST_ASSERT(regex_utf8_decode("\xC3\x28", 2, &index, &cp) == 0);
    index = 0;
    TEST_ASSERT(regex_utf8_decode("\xE2\x28\x93", 3, &index, &cp) == 0);
    index = 0;
    TEST_ASSERT(regex_utf8_decode("\xE2\x9C\x28", 3, &index, &cp) == 0);
    index = 0;
    TEST_ASSERT(regex_utf8_decode("\xF0\x28\x98\x8A", 4, &index, &cp) == 0);
    index = 0;
    TEST_ASSERT(regex_utf8_decode("\xF0\x9F\x28\x8A", 4, &index, &cp) == 0);
    index = 0;
    TEST_ASSERT(regex_utf8_decode("\xF0\x9F\x98\x28", 4, &index, &cp) == 0);

    // Overlong encodings
    index = 0;
    TEST_ASSERT(regex_utf8_decode("\xC1\x81", 2, &index, &cp) == 0);
    index = 0;
    TEST_ASSERT(regex_utf8_decode("\xE0\x83\xB1", 3, &index, &cp) == 0);
    index = 0;
    TEST_ASSERT(regex_utf8_decode("\xF0\x82\x9C\x93", 4, &index, &cp) == 0);

    // Invalid start bytes
    index = 0;
    TEST_ASSERT(regex_utf8_decode("\xFF", 1, &index, &cp) == 0);
    index = 0;
    TEST_ASSERT(regex_utf8_decode("\xF8\x88\x80\x80\x80", 5, &index, &cp) == 0);

    // Out of bounds code point
    index = 0;
    TEST_ASSERT(regex_utf8_decode("\xF4\x90\x80\x80", 4, &index, &cp) == 0);

    return 0;
}

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

int test_ascii_tolower(void) {
    TEST_ASSERT(regex_ascii_tolower('A') == 'a');
    TEST_ASSERT(regex_ascii_tolower('Z') == 'z');
    TEST_ASSERT(regex_ascii_tolower('M') == 'm');
    TEST_ASSERT(regex_ascii_tolower('a') == 'a');
    TEST_ASSERT(regex_ascii_tolower('z') == 'z');
    TEST_ASSERT(regex_ascii_tolower('0') == '0');
    TEST_ASSERT(regex_ascii_tolower('9') == '9');
    TEST_ASSERT(regex_ascii_tolower('!') == '!');
    TEST_ASSERT(regex_ascii_tolower('~') == '~');
    TEST_ASSERT(regex_ascii_tolower(' ') == ' ');
    TEST_ASSERT(regex_ascii_tolower('\n') == '\n');
    TEST_ASSERT(regex_ascii_tolower('\0') == '\0');
    TEST_ASSERT(regex_ascii_tolower(0x1F4A9) == 0x1F4A9);
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

    /* Edge cases */
    TEST_ASSERT(regex_unicode_tolower(0) == 0);
    TEST_ASSERT(regex_unicode_tolower(127) == 127);
    TEST_ASSERT(regex_unicode_tolower(128) == 128);
    TEST_ASSERT(regex_unicode_tolower(255) == 255);
    TEST_ASSERT(regex_unicode_tolower(REGEX_MAX_CODEPOINT) == REGEX_MAX_CODEPOINT);
    TEST_ASSERT(regex_unicode_tolower(0xFFFFFFFF) == 0xFFFFFFFF);

#ifdef REGEX_USE_ICU
    TEST_ASSERT(regex_unicode_tolower(0x00C4) == 0x00E4);
    TEST_ASSERT(regex_unicode_tolower(0x03A9) == 0x03C9);
    TEST_ASSERT(regex_unicode_tolower(0x0410) == 0x0430); /* Cyrillic A */
    TEST_ASSERT(regex_unicode_tolower(0x0100) == 0x0101); /* Latin A with macron */
#else
    TEST_ASSERT(regex_unicode_tolower(0x00C4) == 0x00C4);
    TEST_ASSERT(regex_unicode_tolower(0x03A9) == 0x03A9);
    TEST_ASSERT(regex_unicode_tolower(0x0410) == 0x0410);
    TEST_ASSERT(regex_unicode_tolower(0x0100) == 0x0100);
#endif
    return 0;
}

int test_regex_unicode_tolower_extended(void) {
    /* ASCII Boundaries */
    TEST_ASSERT(regex_unicode_tolower(0) == 0);
    TEST_ASSERT(regex_unicode_tolower('A' - 1) == 'A' - 1);
    TEST_ASSERT(regex_unicode_tolower('Z' + 1) == 'Z' + 1);
    TEST_ASSERT(regex_unicode_tolower(127) == 127);
    TEST_ASSERT(regex_unicode_tolower(128) == 128);

    /* Max Code Point Boundary */
    TEST_ASSERT(regex_unicode_tolower(REGEX_MAX_CODEPOINT) == REGEX_MAX_CODEPOINT);
    TEST_ASSERT(regex_unicode_tolower(REGEX_MAX_CODEPOINT + 1) == REGEX_MAX_CODEPOINT + 1);

#ifdef REGEX_USE_ICU
    /* Cyrillic Capital Letter A to Small Letter A */
    TEST_ASSERT(regex_unicode_tolower(0x0410) == 0x0430);
    /* Cyrillic Capital Letter YA to Small Letter YA */
    TEST_ASSERT(regex_unicode_tolower(0x042F) == 0x044F);

    /* Greek Capital Letter Alpha to Small Letter Alpha */
    TEST_ASSERT(regex_unicode_tolower(0x0391) == 0x03B1);

    /* Latin Capital Letter A with Macron */
    TEST_ASSERT(regex_unicode_tolower(0x0100) == 0x0101);
#else
    /* Fallback ASCII mode: Cyrillic/Greek/Latin extended remain unchanged */
    TEST_ASSERT(regex_unicode_tolower(0x0410) == 0x0410);
    TEST_ASSERT(regex_unicode_tolower(0x042F) == 0x042F);
    TEST_ASSERT(regex_unicode_tolower(0x0391) == 0x0391);
    TEST_ASSERT(regex_unicode_tolower(0x0100) == 0x0100);
#endif

    return 0;
}

int test_unicode_toupper(void) {
    for (uint32_t cp = 'a'; cp <= 'z'; cp++) {
        TEST_ASSERT(regex_unicode_toupper(cp) == cp - 'a' + 'A');
    }
    for (uint32_t cp = 'A'; cp <= 'Z'; cp++) {
        TEST_ASSERT(regex_unicode_toupper(cp) == cp);
    }
    for (uint32_t cp = '0'; cp <= '9'; cp++) {
        TEST_ASSERT(regex_unicode_toupper(cp) == cp);
    }
    TEST_ASSERT(regex_unicode_toupper(0) == 0);
    TEST_ASSERT(regex_unicode_toupper(REGEX_MAX_CODEPOINT) == REGEX_MAX_CODEPOINT);

    TEST_ASSERT(regex_unicode_toupper('a') == 'A');
    TEST_ASSERT(regex_unicode_toupper('z') == 'Z');
    TEST_ASSERT(regex_unicode_toupper('A') == 'A');
    TEST_ASSERT(regex_unicode_toupper('Z') == 'Z');
    TEST_ASSERT(regex_unicode_toupper('0') == '0');
    TEST_ASSERT(regex_unicode_toupper('9') == '9');
    TEST_ASSERT(regex_unicode_toupper('!') == '!');
    TEST_ASSERT(regex_unicode_toupper(' ') == ' ');

    /* Edge cases */
    TEST_ASSERT(regex_unicode_toupper(0) == 0);
    TEST_ASSERT(regex_unicode_toupper(127) == 127);
    TEST_ASSERT(regex_unicode_toupper(128) == 128);
    TEST_ASSERT(regex_unicode_toupper(255) == 255);
    TEST_ASSERT(regex_unicode_toupper(REGEX_MAX_CODEPOINT) == REGEX_MAX_CODEPOINT);
    TEST_ASSERT(regex_unicode_toupper(0xFFFFFFFF) == 0xFFFFFFFF);

#ifdef REGEX_USE_ICU
    TEST_ASSERT(regex_unicode_toupper(0x00E4) == 0x00C4);
    TEST_ASSERT(regex_unicode_toupper(0x03C9) == 0x03A9);
    TEST_ASSERT(regex_unicode_toupper(0x0430) == 0x0410); /* Cyrillic a -> A */
    TEST_ASSERT(regex_unicode_toupper(0x00E9) == 0x00C9); /* e acute */
#else
    TEST_ASSERT(regex_unicode_toupper(0x00E4) == 0x00E4);
    TEST_ASSERT(regex_unicode_toupper(0x03C9) == 0x03C9);
    TEST_ASSERT(regex_unicode_toupper(0x0430) == 0x0430);
    TEST_ASSERT(regex_unicode_toupper(0x00E9) == 0x00E9);
#endif
    return 0;
}

int test_util_unicode_case(void) {
    TEST_ASSERT(regex_unicode_tolower('A') == 'a');
    TEST_ASSERT(regex_unicode_tolower('Z') == 'z');
    TEST_ASSERT(regex_unicode_tolower('a') == 'a');
    TEST_ASSERT(regex_unicode_tolower('z') == 'z');
    TEST_ASSERT(regex_unicode_tolower('1') == '1');
#ifdef REGEX_USE_ICU
    TEST_ASSERT(regex_unicode_tolower(0x00C9) == 0x00E9);
#else
    TEST_ASSERT(regex_unicode_tolower(0x00C9) == 0x00C9);
#endif

    TEST_ASSERT(regex_unicode_toupper('a') == 'A');
    TEST_ASSERT(regex_unicode_toupper('z') == 'Z');
    TEST_ASSERT(regex_unicode_toupper('A') == 'A');
    TEST_ASSERT(regex_unicode_toupper('Z') == 'Z');
    TEST_ASSERT(regex_unicode_toupper('1') == '1');
#ifdef REGEX_USE_ICU
    TEST_ASSERT(regex_unicode_toupper(0x00E9) == 0x00C9);
#else
    TEST_ASSERT(regex_unicode_toupper(0x00E9) == 0x00E9);
#endif

    return 0;
}

extern char *regex_escape_literal(const char *s, size_t len);

int test_util_escape_literal(void) {
    char *escaped;

    // Test NULL input
    escaped = regex_escape_literal(NULL, 0);
    TEST_ASSERT(escaped == NULL);

    // Test empty string
    escaped = regex_escape_literal("", 0);
    TEST_ASSERT(escaped != NULL);
    TEST_ASSERT(strcmp(escaped, "") == 0);
    free(escaped);

    // Test no special characters
    const char *s1 = "hello world 123";
    escaped = regex_escape_literal(s1, strlen(s1));
    TEST_ASSERT(escaped != NULL);
    TEST_ASSERT(strcmp(escaped, "hello world 123") == 0);
    free(escaped);

    // Test all special characters
    const char *s2 = ".*+?()[]{}|^$\\";
    escaped = regex_escape_literal(s2, strlen(s2));
    TEST_ASSERT(escaped != NULL);
    TEST_ASSERT(strcmp(escaped, "\\.\\*\\+\\?\\(\\)\\[\\]\\{\\}\\|\\^\\$\\\\") == 0);
    free(escaped);

    // Test mixed content
    const char *s3 = "a(b)c.*d\\e";
    escaped = regex_escape_literal(s3, strlen(s3));
    TEST_ASSERT(escaped != NULL);
    TEST_ASSERT(strcmp(escaped, "a\\(b\\)c\\.\\*d\\\\e") == 0);
    free(escaped);

    return 0;
}
