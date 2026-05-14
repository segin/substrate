/*
 * lib/c/src/inttypes.c — <inttypes.h> conversion entries.
 *
 * On substrate, intmax_t / uintmax_t are 64-bit (int64_t/uint64_t),
 * the same width as long long / unsigned long long.  That makes
 * strto[u]imax a pure cast over strto[u]ll, and wcstoimax/umax over
 * wcstoll/ull.  imaxabs / imaxdiv are trivial.
 */

#include <inttypes.h>
#include <stdlib.h>
#include <wchar.h>

intmax_t imaxabs(intmax_t j) {
    /* C99 7.8.2.1: behavior is undefined when j == INTMAX_MIN
     * (no representable positive counterpart in two's complement).
     * Leave that case as the natural overflow (returns INTMAX_MIN). */
    return j < 0 ? -j : j;
}

imaxdiv_t imaxdiv(intmax_t numer, intmax_t denom) {
    imaxdiv_t r;
    r.quot = numer / denom;
    r.rem  = numer % denom;
    return r;
}

intmax_t strtoimax(const char *restrict nptr, char **restrict endptr, int base) {
    return (intmax_t)strtoll(nptr, endptr, base);
}

uintmax_t strtoumax(const char *restrict nptr, char **restrict endptr, int base) {
    return (uintmax_t)strtoull(nptr, endptr, base);
}

intmax_t wcstoimax(const wchar_t *restrict nptr, wchar_t **restrict endptr, int base) {
    return (intmax_t)wcstoll(nptr, endptr, base);
}

uintmax_t wcstoumax(const wchar_t *restrict nptr, wchar_t **restrict endptr, int base) {
    return (uintmax_t)wcstoull(nptr, endptr, base);
}
