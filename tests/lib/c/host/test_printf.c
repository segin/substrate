#define _DEFAULT_SOURCE 1
#define _GNU_SOURCE 1
/* Host test: substrate vsnprintf vs glibc snprintf.  Builds the substrate
 * printf.c with its public symbols renamed, then diffs output against the
 * host libc for a battery of directives.  Integer/string/char/pointer/
 * positional cases are strict; float cases are informational (the float
 * formatter is unchanged by this work and may differ from glibc rounding). */
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h>

#define vsnprintf sub_vsnprintf
#define snprintf  sub_snprintf
#define sprintf   sub_sprintf
#define vsprintf  sub_vsprintf
#define printf    sub_printf
#define fprintf   sub_fprintf
#define vprintf   sub_vprintf
#define vfprintf  sub_vfprintf
#define vdprintf  sub_vdprintf
#define dprintf   sub_dprintf
#define asprintf  sub_asprintf
#define vasprintf sub_vasprintf
#define __vasprintf_core sub___vasprintf_core
#include "../../../../lib/c/stdio/printf.c"
#undef vsnprintf
#undef snprintf

static int pass = 0, fail = 0, finfo = 0;

#define CK(strict, expr_sub, expr_ref) do {                                   \
    char a[512], b[512];                                                      \
    expr_sub; expr_ref;                                                       \
    if (strcmp(a, b) == 0) pass++;                                            \
    else if (strict) { fail++; printf("FAIL: got[%s] want[%s]\n", a, b); }    \
    else { finfo++; }                                                         \
} while (0)

#define S(fmt, ...) CK(1, sub_snprintf(a,sizeof a,fmt,##__VA_ARGS__), snprintf(b,sizeof b,fmt,##__VA_ARGS__))
#define F(fmt, ...) CK(0, sub_snprintf(a,sizeof a,fmt,##__VA_ARGS__), snprintf(b,sizeof b,fmt,##__VA_ARGS__))

int main(void) {
    /* integers */
    S("%d", 0); S("%d", 42); S("%d", -42); S("%i", 123456789);
    S("%5d", 42); S("%-5d", 42); S("%05d", 42); S("%+d", 42); S("% d", 42);
    S("%8.4d", 42); S("%-8.4d", 42); S("%.0d", 0); S("%.5d", 123);
    S("%+08d", -7); S("%x", 255); S("%X", 255); S("%#x", 255); S("%#X", 0);
    S("%o", 64); S("%#o", 64); S("%u", 4000000000U); S("%10.6x", 0xab);
    S("%ld", 2147483647L); S("%lld", 9223372036854775807LL);
    S("%lu", 4294967295UL); S("%llx", 0xdeadbeefcafeULL);
    S("%hd", (short)-1); S("%hhd", (signed char)-1); S("%hu", (unsigned short)65535);
    S("%zu", (size_t)123456); S("%zd", (ssize_t)-99); S("%jd", (intmax_t)-1);
    /* strings / chars / pointers */
    S("%s", "hello"); S("%10s", "hi"); S("%-10s", "hi"); S("%.3s", "hello");
    S("%10.3s", "hello"); S("%s", (char*)NULL); S("%c", 'A'); S("%5c", 'A'); S("%-5c", 'B');
    S("%%"); S("100%%done");
    /* width/precision via * */
    S("%*d", 8, 42); S("%-*d", 8, 42); S("%.*f", 2, 3.14159); S("%*.*f", 10, 3, 2.5);
    /* POSITIONAL — the headline feature (the CDE %1$s%2$s%3$s bug) */
    S("%1$s%2$s%3$s", "a", "b", "c");
    S("%2$s %1$s", "world", "hello");
    S("%1$d %2$d %1$d", 7, 9);
    S("%1$s is %2$d years", "Bob", 30);
    S("%3$s%1$s%2$s", "B", "C", "A");
    S("%2$*1$d", 6, 42);             /* width from arg 1, value from arg 2 */
    S("%1$.*2$f", 3.14159, 2);       /* value arg1, precision arg2 */
    S("Either action \"%1$s\" not found for \"%2$s\" attr \"%3$s\"", "Open", "/f", "TEXT");
    /* edge cases touched by the rewrite */
    S("%08.2f", -2.5); S("%+08.1f", 3.5); S("%08.3e", 12.0);
    S("%#o", 0); S("%#o", 8); S("%.0o", 0); S("%#.5x", 0x1f);
    S("%-+ #08.3d", 42); S("%3c", 'Z'); S("%-3c", 'Z');
    S("%2$s/%1$s/%3$s", "b", "a", "c"); S("%1$05d", 7);
    S("[%-10.4s]", "hello world"); S("%+.3d", -5);
    S("%+f", 3.5); S("% f", 3.5); S("%+08.1f", 3.5); S("% 08.1f", 2.0);
    S("%+.1f", -3.5); S("%+e", 12.0); S("%+g", 0.5);
    /* floats (informational) */
    F("%f", 3.14159); F("%.2f", 3.14159); F("%e", 12345.678); F("%g", 0.0001);
    F("%g", 1234567.0); F("%10.3f", -2.5); F("%+.1f", 0.0);

    printf("\n=== printf battery: %d passed, %d FAILED, %d float-info ===\n", pass, fail, finfo);
    return fail ? 1 : 0;
}
