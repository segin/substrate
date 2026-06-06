/*
 * test_delta.c - CPU% delta math (REQ-23-0097).
 *
 * top_cpu_pct() must derive %CPU purely from a jiffy delta over a
 * wall-clock delta, never an instantaneous reading.  Host test.
 *
 *   cc -I../../../bin/top -I../../../include test_delta.c \
 *      ../../../bin/top/top_sort.c -o test_delta && ./test_delta
 */
#include "top.h"
#include <stdio.h>

static int fails = 0;

static void chk(const char *name, double got, double want) {
    double d = got - want; if (d < 0) d = -d;
    if (d > 0.01) { printf("FAIL %-16s got %.4f want %.4f\n", name, got, want); fails++; }
    else          { printf("ok   %-16s = %.2f\n", name, got); }
}

int main(void) {
    /* 50 jiffies over 1.0 s at 100 Hz -> 50% of one CPU. */
    chk("half",          top_cpu_pct(50, 1.0, 100), 50.0);
    /* 100 jiffies / 1 s / 100 Hz -> 100%. */
    chk("full",          top_cpu_pct(100, 1.0, 100), 100.0);
    /* Substrate HZ is 128: 128 jiffies / 1 s / 128 Hz -> 100%. */
    chk("hz128",         top_cpu_pct(128, 1.0, 128), 100.0);
    /* 25 jiffies over half a second at 100 Hz -> 50%. */
    chk("half_interval", top_cpu_pct(25, 0.5, 100), 50.0);
    /* A zero (or negative) interval must yield 0, not a divide blow-up. */
    chk("zero_interval", top_cpu_pct(50, 0.0, 100), 0.0);
    /* An unknown clock rate must yield 0, not divide-by-zero. */
    chk("zero_hz",       top_cpu_pct(50, 1.0, 0), 0.0);
    /* No work done over a real interval -> 0%. */
    chk("idle",          top_cpu_pct(0, 1.0, 128), 0.0);

    printf("%s (%d failure%s)\n", fails ? "FAILED" : "PASSED",
           fails, fails == 1 ? "" : "s");
    return fails ? 1 : 0;
}
