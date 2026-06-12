/*
 * test_symbol_audit.c — link-completeness fixture for tasklist 24
 * (libm math library completion).
 *
 * References every symbol the §24.2–§24.6 work added.  The TEST is the
 * LINK: if any symbol is missing from -lm the link fails.  At run time it
 * just confirms each address is non-NULL and prints the count, so the
 * fixture doubles as a mechanically-verifiable completion marker.
 *
 * Declares the symbols locally (not via <math.h>/<complex.h>) so the probe
 * depends only on the symbols existing in the library, not on the header
 * feature-test guards — name resolution is all that matters here.
 */
#include <stdio.h>

#define SYM(name) extern void name(void);
/* 24.2 Obsolescent aliases */
SYM(scalb) SYM(scalbf)
SYM(significand) SYM(significandf) SYM(significandl)
SYM(drem) SYM(dremf)
SYM(gamma) SYM(gammaf)
SYM(pow10) SYM(pow10f) SYM(pow10l)
SYM(matherr)
/* 24.3 C23 narrowing arithmetic */
SYM(fadd) SYM(faddl) SYM(dadd) SYM(daddl)
SYM(fsub) SYM(fsubl) SYM(dsub) SYM(dsubl)
SYM(fmul) SYM(fmull) SYM(dmul) SYM(dmull)
SYM(fdiv) SYM(fdivl) SYM(ddiv) SYM(ddivl)
SYM(ffma) SYM(ffmal) SYM(dfma) SYM(dfmal)
SYM(fsqrt) SYM(fsqrtl) SYM(dsqrt) SYM(dsqrtl)
/* 24.4 Total order & NaN payload */
SYM(totalorder) SYM(totalorderf) SYM(totalorderl)
SYM(totalordermag) SYM(totalordermagf) SYM(totalordermagl)
SYM(canonicalize) SYM(canonicalizef) SYM(canonicalizel)
SYM(getpayload) SYM(getpayloadf) SYM(getpayloadl)
SYM(setpayload) SYM(setpayloadf) SYM(setpayloadl)
SYM(setpayloadsig) SYM(setpayloadsigf) SYM(setpayloadsigl)
/* 24.5 Reentrant gamma (f/l) */
SYM(lgammaf_r) SYM(lgammal_r)
/* 24.6 Complex base-10 logarithm */
SYM(clog10) SYM(clog10f) SYM(clog10l)
#undef SYM

static void *const probe[] = {
#define REF(name) (void *)&name,
    REF(scalb) REF(scalbf)
    REF(significand) REF(significandf) REF(significandl)
    REF(drem) REF(dremf)
    REF(gamma) REF(gammaf)
    REF(pow10) REF(pow10f) REF(pow10l)
    REF(matherr)
    REF(fadd) REF(faddl) REF(dadd) REF(daddl)
    REF(fsub) REF(fsubl) REF(dsub) REF(dsubl)
    REF(fmul) REF(fmull) REF(dmul) REF(dmull)
    REF(fdiv) REF(fdivl) REF(ddiv) REF(ddivl)
    REF(ffma) REF(ffmal) REF(dfma) REF(dfmal)
    REF(fsqrt) REF(fsqrtl) REF(dsqrt) REF(dsqrtl)
    REF(totalorder) REF(totalorderf) REF(totalorderl)
    REF(totalordermag) REF(totalordermagf) REF(totalordermagl)
    REF(canonicalize) REF(canonicalizef) REF(canonicalizel)
    REF(getpayload) REF(getpayloadf) REF(getpayloadl)
    REF(setpayload) REF(setpayloadf) REF(setpayloadl)
    REF(setpayloadsig) REF(setpayloadsigf) REF(setpayloadsigl)
    REF(lgammaf_r) REF(lgammal_r)
    REF(clog10) REF(clog10f) REF(clog10l)
#undef REF
};

int main(void)
{
    const unsigned n = (unsigned)(sizeof(probe) / sizeof(probe[0]));
    unsigned resolved = 0;
    for (unsigned i = 0; i < n; i++)
        if (probe[i]) resolved++;
    printf("symbol-audit: %u/%u tasklist-24 symbols resolved\n", resolved, n);
    return resolved == n ? 0 : 1;
}
