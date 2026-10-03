/*
 * lazymod.c - libld64lazy.so, the functions lazy.c reaches through
 * lazily bound PLT slots.  Each mixes its arguments into one value, so a
 * register the binding trampoline failed to preserve shows up in the
 * result of the FIRST call.
 */
#include <stdarg.h>

/* Six integer registers and two words on the stack. */
long lazymod_ints(long a, long b, long c, long d, long e, long f,
                  long g, long h) {
    return a + 2 * b + 3 * c + 4 * d + 5 * e + 6 * f + 7 * g + 8 * h;
}

/* Eight vector registers and one double on the stack. */
double lazymod_doubles(double a, double b, double c, double d, double e,
                       double f, double g, double h, double i) {
    return a + 2 * b + 3 * c + 4 * d + 5 * e + 6 * f + 7 * g + 8 * h + 9 * i;
}

/* A variadic call: %al carries the number of vector registers used, and
 * the prologue saves them only if it is non-zero. */
double lazymod_sum(int n, ...) {
    va_list ap;
    double s = 0;
    va_start(ap, n);
    for (int i = 0; i < n; i++) {
        s += va_arg(ap, int);
        s += va_arg(ap, double);
    }
    va_end(ap);
    return s;
}

long lazymod_calls;
void lazymod_tick(void) { lazymod_calls++; }
