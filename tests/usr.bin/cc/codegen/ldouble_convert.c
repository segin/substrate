/*
 * Converting a long double to double or float rounds it.  On x86-64 a
 * long double is 16 bytes holding x87 extended precision, and the
 * conversion was missing: the double was the long double's low 8 bytes,
 * part of its mantissa, so 0.5L became -0.  Explicit casts, initializers,
 * assignments, stores through members and array elements, returns and
 * arguments all convert.
 */
#include <stdio.h>
#include <string.h>

static long double gx = 0.5L;

struct s {
    double d;
    float f;
};

static double cast_d(long double v) {
    return (double)v;
}

static float cast_f(long double v) {
    return (float)v;
}

static double ret_d(long double v) {
    return v;
}

static float ret_f(long double v) {
    return v;
}

static double take_d(double d) {
    return d * 2;
}

static float take_f(float f) {
    return f * 4;
}

int main(void) {
    long double x = 0.5L;
    double d = x;
    float f = x;
    double d2;
    float f2;
    struct s st;
    double arr[2];
    union {
        double d;
        unsigned char b[8];
    } u;

    d2 = x * 3;
    f2 = gx;
    st.d = x;
    st.f = x;
    arr[1] = gx + 1;
    u.d = (double)gx;
    printf("%g %g %g %g\n", d, (double)f, d2, (double)f2);
    printf("%g %g %g %g\n", cast_d(0.75L), (double)cast_f(1.5L), ret_d(0.25L), (double)ret_f(0.125L));
    printf("%g %g %g %g %g\n", take_d(x), (double)take_f(x), st.d, (double)st.f, arr[1]);
    printf("%g %02x%02x\n", (double)(x * 2), u.b[7], u.b[6]);
    return 0;
}
