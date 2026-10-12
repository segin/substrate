#include "as_data.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * What as_data.c is: the bytes a string literal says, and LEB128.
 * (What data directives make of them is the emitter's, and is held to
 * GNU as's by test_data_core.sh.)
 */

static int failed;

static void literal(const char *src, const char *want, size_t want_len) {
    char *bytes = NULL;
    size_t len = 0;

    if (as_decode_string_literal(src, &bytes, &len) != 0) {
        fprintf(stderr, "FAIL [%s]: not decoded\n", src);
        failed = 1;
        return;
    }
    if (len != want_len || memcmp(bytes, want, want_len) != 0 || bytes[len] != '\0') {
        fprintf(stderr, "FAIL [%s]: %zu bytes, not the %zu expected\n", src, len, want_len);
        failed = 1;
    }
    free(bytes);
}

static void leb(long long value, int is_signed, const char *want, size_t want_len) {
    unsigned char out[10];
    size_t n = as_leb128_encode(value, is_signed, out);

    if (n != want_len || memcmp(out, want, want_len) != 0) {
        fprintf(stderr, "FAIL leb128 of %lld (%s): %zu bytes, not the %zu expected\n", value,
                is_signed ? "signed" : "unsigned", n, want_len);
        failed = 1;
    }
}

int main(void) {
    char *bytes = NULL;
    size_t len = 0;

    literal("", "", 0);
    literal("AB", "AB", 2);
    literal(":", ":", 1);
    literal("a\\nb", "a\nb", 3);
    literal("\\t\\r\\b\\f\\v", "\t\r\b\f\v", 5);
    literal("\\\\\\\"\\'\\?", "\\\"'?", 4);
    /* GNU as has no escape for a bell: `\a` is an a. */
    literal("\\a", "a", 1);
    literal("\\x41\\X42", "AB", 2);
    literal("\\x", "\0", 1);
    literal("\\101\\0", "A\0", 2);
    /* Octal of up to three digits, 8 and 9 among them: `\18` is 16. */
    literal("\\18", "\020", 1);
    literal("\\1012", "A2", 2);
    literal("a\\0b", "a\0b", 3);
    /* A `\` with nothing after it is no literal. */
    if (as_decode_string_literal("abc\\", &bytes, &len) == 0) {
        fprintf(stderr, "FAIL: a literal ending in a backslash was decoded\n");
        failed = 1;
        free(bytes);
    }
    if (as_decode_string_literal("x", NULL, &len) == 0 || as_decode_string_literal("x", &bytes, NULL) == 0) {
        fprintf(stderr, "FAIL: decoded to nowhere\n");
        failed = 1;
    }

    leb(0, 0, "\x00", 1);
    leb(127, 0, "\x7f", 1);
    leb(128, 0, "\x80\x01", 2);
    leb(624485, 0, "\xe5\x8e\x26", 3);
    leb(-1, 0, "\xff\xff\xff\xff\xff\xff\xff\xff\xff\x01", 10);
    leb(0, 1, "\x00", 1);
    leb(63, 1, "\x3f", 1);
    leb(64, 1, "\xc0\x00", 2);
    leb(-1, 1, "\x7f", 1);
    leb(-64, 1, "\x40", 1);
    leb(-65, 1, "\xbf\x7f", 2);
    leb(-123456, 1, "\xc0\xbb\x78", 3);

    return failed;
}
