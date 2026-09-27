/*
 * torture_mbrtowc.c -- mbrtowc() keeps a partial character in its state.
 *
 * Given an incomplete UTF-8 sequence, mbrtowc() returned (size_t)-2 but
 * left the mbstate_t untouched, so the rest of the character was then
 * rejected as invalid; mbsinit() said "initial state" throughout, and no
 * error set errno.  gnulib's configure checks exactly this ("whether
 * mbrtowc handles incomplete characters"); failing it makes gnulib replace
 * mbstate_t, which broke building grep 3.12 on Substrate.
 *
 *   gnulib    the configure check itself;
 *   bytewise  "Büßer" and a 4-byte character fed one byte at a
 *             time decode to the right characters;
 *   mbsinit   non-initial while a character is incomplete, initial after;
 *   reset     mbrtowc(NULL, NULL, 0, &st) returns to the initial state;
 *   eilseq    an invalid byte returns (size_t)-1 with errno EILSEQ, also
 *             in the middle of a character;
 *   mbrlen    byte-wise, with its own state;
 *   mbsrtowcs finishes a character begun with mbrtowc on the same state.
 *
 * Prints a "Result:" line.
 */
#include <errno.h>
#include <locale.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

static int failures;

static void check(const char *name, int ok) {
    printf("  %s %s\n", ok ? "ok  " : "FAIL", name);
    if (!ok)
        failures++;
}

/* Decode `s` one byte at a time; store the characters in out[]. */
static int bytewise(const char *s, size_t len, wchar_t *out, size_t max) {
    mbstate_t st;
    size_t n = 0;
    memset(&st, 0, sizeof(st));
    for (size_t i = 0; i < len; i++) {
        wchar_t wc;
        size_t r = mbrtowc(&wc, s + i, 1, &st);
        if (r == (size_t)-2)
            continue;
        if (r == (size_t)-1 || n == max)
            return -1;
        out[n++] = wc;
    }
    return (int)n;
}

int main(void) {
    printf("torture_mbrtowc\n");
    setlocale(LC_ALL, "C.UTF-8");

    /* gnulib's gl_MBRTOWC_INCOMPLETE_STATE, UTF-8 variant. */
    {
        const char input[] = "B\303\274\303\237er";
        mbstate_t state;
        wchar_t wc;
        memset(&state, 0, sizeof(state));
        int bad = mbrtowc(&wc, input + 1, 1, &state) == (size_t)-2 && mbsinit(&state);
        check("gnulib: an incomplete character leaves a non-initial state", !bad);
    }

    {
        const char s[] = "B\303\274\303\237er\360\237\230\200!";
        wchar_t out[16];
        int n = bytewise(s, sizeof(s) - 1, out, 16);
        const wchar_t want[] = { 'B', 0xfc, 0xdf, 'e', 'r', 0x1f600, '!' };
        check("bytewise: characters decoded one byte at a time",
              n == 7 && memcmp(out, want, sizeof(want)) == 0);
    }

    {
        mbstate_t st;
        wchar_t wc;
        memset(&st, 0, sizeof(st));
        int ok = mbsinit(&st);
        ok = ok && mbrtowc(&wc, "\342", 1, &st) == (size_t)-2 && !mbsinit(&st);
        ok = ok && mbrtowc(&wc, "\202", 1, &st) == (size_t)-2 && !mbsinit(&st);
        ok = ok && mbrtowc(&wc, "\254", 1, &st) == 1 && wc == 0x20ac && mbsinit(&st);
        check("mbsinit: tracks an incomplete character (EURO SIGN)", ok);

        mbrtowc(&wc, "\342", 1, &st);
        mbrtowc(NULL, NULL, 0, &st);
        check("reset: mbrtowc(NULL, NULL, 0, st) returns to the initial state",
              mbsinit(&st) && mbrtowc(&wc, "A", 1, &st) == 1 && wc == 'A');
    }

    {
        mbstate_t st;
        wchar_t wc;
        memset(&st, 0, sizeof(st));
        errno = 0;
        int ok = mbrtowc(&wc, "\377", 1, &st) == (size_t)-1 && errno == EILSEQ;
        memset(&st, 0, sizeof(st));
        mbrtowc(&wc, "\303", 1, &st);
        errno = 0;
        ok = ok && mbrtowc(&wc, "A", 1, &st) == (size_t)-1 && errno == EILSEQ;
        check("eilseq: invalid bytes fail with EILSEQ, mid-character too", ok);
    }

    {
        mbstate_t st;
        memset(&st, 0, sizeof(st));
        int ok = mbrlen("\303", 1, &st) == (size_t)-2 && mbrlen("\274", 1, &st) == 1;
        check("mbrlen: byte-wise with a state", ok);
    }

    {
        mbstate_t st;
        wchar_t wc, out[8];
        memset(&st, 0, sizeof(st));
        mbrtowc(&wc, "\303", 1, &st);               /* first byte of u-umlaut */
        const char *rest = "\274x";
        size_t n = mbsrtowcs(out, &rest, 8, &st);
        check("mbsrtowcs: finishes a character begun with mbrtowc",
              n == 2 && out[0] == 0xfc && out[1] == 'x' && rest == NULL);
    }

    printf("Result: %s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
