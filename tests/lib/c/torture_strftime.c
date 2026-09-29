/*
 * torture_strftime.c -- strftime() %s is seconds since the Epoch.
 *
 * %s used to expand to nothing, so `date +%s` got a zero-length result
 * and reported "format result too long".  For several instants t,
 * strftime("%s", gmtime(&t)) must print t, and %s must combine with other
 * conversions and literal text.
 *
 * Prints a "Result:" line.
 */
#include <stdio.h>
#include <string.h>
#include <time.h>

static int failures;

static void check_s(time_t t)
{
    char got[64], want[64];
    struct tm *tm = gmtime(&t);

    snprintf(want, sizeof(want), "%lld", (long long)t);
    if (tm == NULL || strftime(got, sizeof(got), "%s", tm) == 0 ||
        strcmp(got, want) != 0) {
        printf("FAIL: %%s for %lld gave '%s'\n", (long long)t,
               tm ? got : "(gmtime failed)");
        failures++;
    }
}

int main(void)
{
    char buf[64];
    time_t t = 1000000000;      /* 2001-09-09 01:46:40 UTC */

    check_s(0);
    check_s(t);
    check_s(1234567890);
    check_s(time(NULL));

    if (strftime(buf, sizeof(buf), "[%s] %Y-%m-%d", gmtime(&t)) == 0 ||
        strcmp(buf, "[1000000000] 2001-09-09") != 0) {
        printf("FAIL: combined format gave '%s'\n", buf);
        failures++;
    }

    printf("Result: %s (%d failure%s)\n", failures ? "FAIL" : "PASS",
           failures, failures == 1 ? "" : "s");
    return failures != 0;
}
