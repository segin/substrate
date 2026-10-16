/*
 * Complex arithmetic: + - * / on every pairing of float, double and long
 * double _Complex with each other and with real and integer operands,
 * conversions both ways, comparisons, truth, GNU __real__/__imag__ (also
 * as lvalues), conjugation by ~, imaginary constants and
 * __builtin_complex.  Each part is printed to the bit, so that the result
 * must be gcc's, rounding included.
 */
#include <stdio.h>

static void pd(const char *what, double _Complex z) {
    printf("%s %a %a\n", what, __real__ z, __imag__ z);
}

static void pf(const char *what, float _Complex z) {
    printf("%s %a %a\n", what, (double)__real__ z, (double)__imag__ z);
}

static void pl(const char *what, long double _Complex z) {
    printf("%s %La %La\n", what, __real__ z, __imag__ z);
}

/* static complex objects: constant initializers folded to their parts */
static double _Complex g_d = 1.0 + 2.0i;
static float _Complex g_f = 3.0f;
static long double _Complex g_l = 2.5Li * 2;
static double _Complex g_arr[3] = {1.0, 2.0i, -0.5 + 0.25i};

static double _Complex twice(double _Complex z) {
    return z + z;
}

static double _Complex from_int(int n) {
    return n;
}

int main(void) {
    double _Complex a = 1.5 + 2.25i;
    double _Complex b = -0.75 + 3.0i;
    float _Complex f = 0.1f + 0.7fi;
    float _Complex g = 3.3f - 1.1fi;
    long double _Complex l = 1.0L / 3.0L + 2.0Li;
    double _Complex z;
    double r;
    int n = 3;

    pd("add", a + b);
    pd("sub", a - b);
    pd("mul", a * b);
    pd("div", a / b);
    pd("div2", b / a); /* |c| < |d| and the other way: both of Smith's branches */
    pd("rdiv", 2.0 / a);
    pd("divr", a / 4.0);
    pd("radd", 1.0 + a);
    pd("rsub", 1.0 - a);
    pd("imul", n * a);
    pd("neg", -a);
    pd("conj", ~a);
    pd("imag", 2.0i * a);
    pf("fmul", f * g); /* float parts round to float at each step */
    pf("fdiv", f / g);
    pf("fadd", f + 1);
    pd("mixed", f * a); /* float _Complex times double _Complex is double _Complex */
    pl("lmul", l * l);
    pl("ldiv", l / (1.0L - 1.0Li));
    pl("lmix", l + a);

    z = a;
    z += b;
    z *= 2;
    z -= 1.0i;
    z /= b;
    pd("compound", z);
    pd("call", twice(a));
    pd("fromint", from_int(7));
    pd("builtin", __builtin_complex(-1.0, 0.5));

    r = a; /* to real: the real part */
    printf("real %a %d %d\n", r, (int)b, (_Bool)(0.0 + 0.0i));
    printf("cmp %d %d %d %d\n", a == a, a != b, a == 1.5 + 2.25i, 2.0 == (2.0 + 0.0i));
    printf("truth %d %d %d\n", !a, !(0.0 + 0.0i), (0.0 + 1.0i) ? 1 : 0);

    __real__ z = 4.0;
    __imag__ z = -8.0;
    pd("parts", z);
    printf("ofreal %a %a\n", __real__ r, __imag__ r);
    printf("sizes %d %d %d\n", (int)sizeof(__real__ f), (int)sizeof(__imag__ a), (int)sizeof(f * a));
    pd("static", g_d);
    pf("staticf", g_f);
    pl("staticl", g_l);
    pd("arr0", g_arr[0]);
    pd("arr1", g_arr[1]);
    pd("arr2", g_arr[2]);
    return 0;
}
