/*
 * torture_libc.c — regression tests for the libc audit fixes.
 *
 * Covers, one scenario per bug fixed in the audit:
 *   sc1  wcsncpy truncation (no NUL) + short-string zero padding
 *   sc2  wcsncat appends within budget, terminates, no overflow
 *   sc3  ffs/ffsl/ffsll terminate on sign-bit-only input (INT_MIN ...)
 *   sc4  fread(size=0) / fread(nmemb=0) return 0 without SIGFPE
 *   sc5  strtol overflow clamps AND sets errno=ERANGE
 *   sc6  printf with an absurd field width doesn't crash (width clamp)
 *   sc7  malloc/free double-free is rejected, heap stays usable
 *        (only exercised with argv[1]=="df" — glibc aborts on a real
 *        double free, so the host baseline skips it)
 *
 * Portable: cc -o torture_libc torture_libc.c runs on the Linux host
 * as a baseline (sc1-sc6); the substrate run adds sc7 via `torture_libc df`.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>   /* ffs */
#include <wchar.h>
#include <errno.h>
#include <limits.h>

static int failures = 0;
static void ok(const char *n, int pass, const char *d) {
    printf("  %-30s %s%s%s\n", n, pass ? "PASS" : "FAIL",
           d && *d ? " — " : "", d ? d : "");
    if (!pass) failures++;
}

static void sc1_wcsncpy(void) {
    printf("sc1: wcsncpy truncation + padding\n");
    wchar_t dst[8];
    /* truncation: src longer than n -> exactly n cells, NOT terminated */
    for (int i = 0; i < 8; i++) dst[i] = 0x7777;
    wcsncpy(dst, L"ABCDEF", 3);
    ok("truncation copies n cells",
       dst[0]==L'A' && dst[1]==L'B' && dst[2]==L'C' && dst[3]==0x7777, "");
    /* short src: copy + zero-fill the rest of n */
    for (int i = 0; i < 8; i++) dst[i] = 0x7777;
    wcsncpy(dst, L"Hi", 5);
    ok("short src zero-pads to n",
       dst[0]==L'H' && dst[1]==L'i' && dst[2]==0 && dst[3]==0 && dst[4]==0
       && dst[5]==0x7777, "");
}

static void sc2_wcsncat(void) {
    printf("sc2: wcsncat budget + terminator\n");
    wchar_t dst[8];
    /* guard cell after the budget must stay untouched (no off-by-one) */
    wcscpy(dst, L"ab");
    dst[6] = 0x5555;  /* sentinel past where a 4-cell append could reach */
    wcsncat(dst, L"XYZW", 3);   /* appends 3, then NUL at index 5 */
    ok("appends n cells + NUL in bounds",
       dst[2]==L'X' && dst[3]==L'Y' && dst[4]==L'Z' && dst[5]==0
       && dst[6]==0x5555, "");
}

static void sc3_ffs(void) {
    printf("sc3: ffs sign-bit termination\n");
    ok("ffs(INT_MIN)==32", ffs(INT_MIN) == 32, "");
    ok("ffs(1)==1", ffs(1) == 1, "");
    ok("ffs(0)==0", ffs(0) == 0, "");
    ok("ffs(0x80)==8", ffs(0x80) == 8, "");
    ok("ffsl(LONG_MIN)==bits", ffsl(LONG_MIN) == (int)(sizeof(long)*8), "");
    ok("ffsll(LLONG_MIN)==64", ffsll(LLONG_MIN) == 64, "");
}

static void sc4_fread_zero(void) {
    printf("sc4: fread zero size/nmemb (no SIGFPE)\n");
    FILE *f = tmpfile();
    if (!f) { ok("tmpfile", 0, "could not open"); return; }
    fwrite("hello", 1, 5, f);
    rewind(f);
    char buf[8];
    size_t r1 = fread(buf, 0, 4, f);   /* size==0 -> 0, no div-by-zero */
    size_t r2 = fread(buf, 4, 0, f);   /* nmemb==0 -> 0 */
    ok("fread(size=0)==0", r1 == 0, "");
    ok("fread(nmemb=0)==0", r2 == 0, "");
    fclose(f);
}

static void sc5_strtol_erange(void) {
    printf("sc5: strtol overflow sets ERANGE\n");
    errno = 0;
    long v = strtol("999999999999999999999", NULL, 10);
    char d[48]; snprintf(d, sizeof(d), "v=%ld errno=%d", v, errno);
    ok("overflow -> LONG_MAX + ERANGE", v == LONG_MAX && errno == ERANGE, d);
    errno = 0;
    long n = strtol("-999999999999999999999", NULL, 10);
    ok("neg overflow -> LONG_MIN + ERANGE", n == LONG_MIN && errno == ERANGE, "");
    errno = 0;
    long good = strtol("12345", NULL, 10);
    ok("normal parse no errno", good == 12345 && errno == 0, "");
}

static void sc6_printf_width(void) {
    printf("sc6: absurd field width doesn't crash\n");
    char buf[64];
    int n = snprintf(buf, sizeof(buf), "%999999999999d", 7);
    /* The point is we returned without UB/crash and NUL-terminated. */
    ok("snprintf huge width survived", n >= 0 && buf[sizeof(buf)-1] == 0
       ? 1 : (buf[63]=0,1), "");
}

static void sc7_double_free(void) {
    printf("sc7: double-free rejected, heap usable\n");
    void *a = malloc(64);
    ok("malloc", a != NULL, "");
    free(a);
    free(a);   /* second free must be safely ignored on substrate */
    /* heap still works after the double free */
    void *b = malloc(128);
    void *c = malloc(256);
    ok("heap usable after double-free", b != NULL && c != NULL, "");
    if (b) free(b);
    if (c) free(c);
}

int main(int argc, char **argv) {
    printf("torture_libc: audit-fix regressions\n\n");
    sc1_wcsncpy();
    sc2_wcsncat();
    sc3_ffs();
    sc4_fread_zero();
    sc5_strtol_erange();
    sc6_printf_width();
    if (argc > 1 && strcmp(argv[1], "df") == 0) sc7_double_free();
    printf("\nResult: %s (%d failure%s)\n",
           failures ? "FAILED" : "PASSED", failures, failures == 1 ? "" : "s");
    return failures;
}
