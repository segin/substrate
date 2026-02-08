/*
 * math.c - Math library functions
 *
 * Implements exponential, logarithmic, and trigonometric functions
 * using Taylor series approximations and mathematical identities.
 */

#include <math.h>
#include <stdint.h>

/* Constants (with guards to avoid redefinition) */
#ifndef M_PI
#define M_PI      3.14159265358979323846
#endif
#ifndef M_PI_2
#define M_PI_2    1.57079632679489661923
#endif
#ifndef M_E
#define M_E       2.71828182845904523536
#endif
#ifndef M_LN2
#define M_LN2     0.69314718055994530942
#endif
#ifndef M_LN10
#define M_LN10    2.30258509299404568402
#endif
#ifndef M_LOG2E
#define M_LOG2E   1.44269504088896340736
#endif

/*
 * exp(x) - e^x using Taylor series
 * e^x = 1 + x + x^2/2! + x^3/3! + ...
 */
double exp(double x) {
    if (isnan(x)) return x;
    if (x == 0.0) return 1.0;
    if (isinf(x)) return (x > 0) ? INFINITY : 0.0;

    double res;
    __asm__ __volatile__(
        "fldl2e\n\t"        /* Load log2(e) */
        "fmulp\n\t"         /* st(0) = x * log2(e) */
        "fld %%st(0)\n\t"
        "frndint\n\t"       /* st(0) = i = round(x * log2(e)) */
        "fsub %%st(0), %%st(1)\n\t" /* st(1) = f = (x * log2(e)) - i */
        "fxch\n\t"
        "f2xm1\n\t"         /* st(0) = 2^f - 1 */
        "fld1\n\t"
        "faddp\n\t"         /* st(0) = 2^f */
        "fscale\n\t"        /* st(0) = 2^f * 2^i = 2^(x * log2(e)) = e^x */
        "fstp %%st(1)\n\t"
        "fstpl %0"
        : "=m"(res) : "m"(x) : "ax", "cc");
    return res;
}

/* exp2(x) = 2^x = e^(x * ln(2)) */
double exp2(double x) {
    if (isnan(x)) return x;
    if (x == 0.0) return 1.0;
    if (isinf(x)) return (x > 0) ? INFINITY : 0.0;

    double res;
    __asm__ __volatile__(
        "fldl %1\n\t"
        "fld %%st(0)\n\t"
        "frndint\n\t"       /* i */
        "fsub %%st(0), %%st(1)\n\t" /* f */
        "fxch\n\t"
        "f2xm1\n\t"
        "fld1\n\t"
        "faddp\n\t"
        "fscale\n\t"
        "fstp %%st(1)\n\t"
        "fstpl %0"
        : "=m"(res) : "m"(x) : "ax", "cc");
    return res;
}

/* expm1(x) = e^x - 1, accurate for small x */
double expm1(double x) {
    if (fabs(x) < 1e-9) return x + 0.5 * x * x;  /* Taylor for small x */
    return exp(x) - 1.0;
}

/*
 * log(x) - natural logarithm using Newton-Raphson on exp
 * Uses identity: log(x) = 2 * atanh((x-1)/(x+1)) for x > 0
 */
double log(double x) {
    double res;
    __asm__ __volatile__("fldln2; fldl %1; fyl2x; fstpl %0" : "=m"(res) : "m"(x));
    return res;
}

/* log2(x) = log(x) / ln(2) */
double log2(double x) {
    double res;
    __asm__ __volatile__("fld1; fldl %1; fyl2x; fstpl %0" : "=m"(res) : "m"(x));
    return res;
}

/* log10(x) = log(x) / ln(10) */
double log10(double x) {
    double res;
    __asm__ __volatile__("fldlg2; fldl %1; fyl2x; fstpl %0" : "=m"(res) : "m"(x));
    return res;
}

/* log1p(x) = log(1+x), accurate for small x */
double log1p(double x) {
    if (fabs(x) < 1e-9) return x - 0.5 * x * x;  /* Taylor for small x */
    return log(1.0 + x);
}

/* pow(x, y) = x^y = e^(y * log(x)) */
double pow(double x, double y) {
    if (y == 0.0) return 1.0;
    if (x == 1.0) return 1.0;
    if (x == 0.0) return (y > 0) ? 0.0 : INFINITY;
    if (isnan(x) || isnan(y)) return NAN;

    if (x < 0.0) {
        /* Negative base: only valid if y is an integer */
        double yi;
        if (modf(y, &yi) != 0.0) return NAN;
        double res = pow(-x, y);
        if (((long long)yi) % 2) return -res;
        return res;
    }

    double res;
    __asm__ __volatile__(
        "fldl %2\n\t"       /* y */
        "fldl %1\n\t"       /* x */
        "fyl2x\n\t"         /* st(0) = y * log2(x) */
        "fld %%st(0)\n\t"
        "frndint\n\t"       /* i */
        "fsub %%st(0), %%st(1)\n\t" /* f */
        "fxch\n\t"
        "f2xm1\n\t"
        "fld1\n\t"
        "faddp\n\t"
        "fscale\n\t"
        "fstp %%st(1)\n\t"
        "fstpl %0"
        : "=m"(res) : "m"(x), "m"(y) : "ax", "cc");
    return res;
}

/*
 * sqrt(x) - Square root using Newton-Raphson
 * x_{n+1} = 0.5 * (x_n + S/x_n)
 */
double sqrt(double x) {
    if (x < 0) return NAN;
    double res;
    __asm__ __volatile__("fldl %1; fsqrt; fstpl %0" : "=m"(res) : "m"(x));
    return res;
}

/* cbrt(x) - Cube root using Newton-Raphson */
double cbrt(double x) {
    if (x == 0) return 0.0;
    
    int neg = (x < 0);
    if (neg) x = -x;
    
    /* Initial guess */
    double guess = x * 0.5;
    if (guess == 0) guess = 1.0;
    
    /* Newton-Raphson: x_{n+1} = (2*x_n + S/x_n^2) / 3 */
    for (int i = 0; i < 20; i++) {
        double next = (2.0 * guess + x / (guess * guess)) / 3.0;
        if (fabs(next - guess) < 1e-15 * fabs(guess)) break;
        guess = next;
    }
    
    return neg ? -guess : guess;
}

/* hypot(x, y) = sqrt(x^2 + y^2), avoiding overflow */
double hypot(double x, double y) {
    x = fabs(x);
    y = fabs(y);
    if (x < y) { double t = x; x = y; y = t; }
    if (x == 0) return 0.0;
    double r = y / x;
    return x * sqrt(1.0 + r * r);
}

/*
 * sin(x) - Sine using Taylor series
 * sin(x) = x - x^3/3! + x^5/5! - ...
 */
double sin(double x) {
    double res;
    __asm__ __volatile__(
        "fldl %1\n\t"
        "1: fsin\n\t"
        "fnstsw %%ax\n\t"
        "sahf\n\t"
        "jp 2f\n\t"
        "fstpl %0\n\t"
        "jmp 3f\n\t"
        "2: fldpi\n\t"
        "fadd %%st(0)\n\t"
        "fxch %%st(1)\n\t"
        "fprem1\n\t"
        "fstp %%st(1)\n\t"
        "jmp 1b\n\t"
        "3:"
        : "=m"(res) : "m"(x) : "ax", "cc");
    return res;
}

/*
 * cos(x) - Cosine using Taylor series
 * cos(x) = 1 - x^2/2! + x^4/4! - ...
 */
double cos(double x) {
    double res;
    __asm__ __volatile__(
        "fldl %1\n\t"
        "1: fcos\n\t"
        "fnstsw %%ax\n\t"
        "sahf\n\t"
        "jp 2f\n\t"
        "fstpl %0\n\t"
        "jmp 3f\n\t"
        "2: fldpi\n\t"
        "fadd %%st(0)\n\t"
        "fxch %%st(1)\n\t"
        "fprem1\n\t"
        "fstp %%st(1)\n\t"
        "jmp 1b\n\t"
        "3:"
        : "=m"(res) : "m"(x) : "ax", "cc");
    return res;
}

void sincos(double x, double *s, double *c) {
    __asm__ __volatile__(
        "fldl %2\n\t"
        "1: fsincos\n\t"
        "fnstsw %%ax\n\t"
        "sahf\n\t"
        "jp 2f\n\t"
        "fstpl %1\n\t"
        "fstpl %0\n\t"
        "jmp 3f\n\t"
        "2: fldpi\n\t"
        "fadd %%st(0)\n\t"
        "fxch %%st(1)\n\t"
        "fprem1\n\t"
        "fstp %%st(1)\n\t"
        "jmp 1b\n\t"
        "3:"
        : "=m"(*s), "=m"(*c) : "m"(x) : "ax", "cc");
}

/* tan(x) = sin(x) / cos(x) */
double tan(double x) {
    double res;
    __asm__ __volatile__(
        "fldl %1\n\t"
        "1: fptan\n\t"
        "fnstsw %%ax\n\t"
        "sahf\n\t"
        "jp 2f\n\t"
        "fstp %%st(0)\n\t" // Pop the 1.0 pushed by fptan
        "fstpl %0\n\t"
        "jmp 3f\n\t"
        "2: fldpi\n\t"
        "fadd %%st(0)\n\t"
        "fxch %%st(1)\n\t"
        "fprem1\n\t"
        "fstp %%st(1)\n\t"
        "jmp 1b\n\t"
        "3:"
        : "=m"(res) : "m"(x) : "ax", "cc");
    return res;
}

/*
 * atan(x) - Arctangent using Taylor series
 * atan(x) = x - x^3/3 + x^5/5 - ... for |x| <= 1
 */
double atan(double x) {
    int neg = (x < 0);
    if (neg) x = -x;
    
    int inv = (x > 1.0);
    if (inv) x = 1.0 / x;
    
    /* Taylor series for |x| <= 1 */
    double x2 = x * x;
    double term = x, sum = x;
    for (int i = 1; i < 50 && fabs(term) > 1e-15; i++) {
        term *= -x2;
        sum += term / (2 * i + 1);
    }
    
    if (inv) sum = M_PI_2 - sum;
    return neg ? -sum : sum;
}

/* atan2(y, x) - Two-argument arctangent */
double atan2(double y, double x) {
    double res;
    __asm__ __volatile__("fldl %1; fldl %2; fpatan; fstpl %0" : "=m"(res) : "m"(y), "m"(x));
    return res;
}

/*
 * asin(x) - Arcsine using identity
 * asin(x) = atan(x / sqrt(1 - x^2))
 */
double asin(double x) {
    if (x < -1.0 || x > 1.0) return NAN;
    if (x == 1.0) return M_PI_2;
    if (x == -1.0) return -M_PI_2;
    return atan(x / sqrt(1.0 - x * x));
}

/* acos(x) = pi/2 - asin(x) */
double acos(double x) {
    if (x < -1.0 || x > 1.0) return NAN;
    return M_PI_2 - asin(x);
}

/*
 * Hyperbolic functions
 * sinh(x) = (e^x - e^-x) / 2
 * cosh(x) = (e^x + e^-x) / 2
 * tanh(x) = sinh(x) / cosh(x)
 */
double sinh(double x) {
    if (fabs(x) < 1e-9) return x;  /* Taylor for small x */
    double ex = exp(x);
    return (ex - 1.0 / ex) * 0.5;
}

double cosh(double x) {
    double ex = exp(x);
    return (ex + 1.0 / ex) * 0.5;
}

double tanh(double x) {
    if (x > 20.0) return 1.0;
    if (x < -20.0) return -1.0;
    double e2x = exp(2.0 * x);
    return (e2x - 1.0) / (e2x + 1.0);
}

/* asinh(x) = log(x + sqrt(x^2 + 1)) */
double asinh(double x) {
    if (fabs(x) < 1e-9) return x;
    return log(x + sqrt(x * x + 1.0));
}

/* acosh(x) = log(x + sqrt(x^2 - 1)), x >= 1 */
double acosh(double x) {
    if (x < 1.0) return NAN;
    return log(x + sqrt(x * x - 1.0));
}

/* atanh(x) = 0.5 * log((1+x)/(1-x)), |x| < 1 */
double atanh(double x) {
    if (x <= -1.0 || x >= 1.0) return (x == 1.0) ? INFINITY : (x == -1.0) ? -INFINITY : NAN;
    return 0.5 * log((1.0 + x) / (1.0 - x));
}

/*
 * Floating-point manipulation functions
 */

/* frexp: x = mantissa * 2^exp, where 0.5 <= |mantissa| < 1 */
double frexp(double x, int *exp) {
    if (x == 0.0) { *exp = 0; return 0.0; }
    if (isinf(x) || isnan(x)) { *exp = 0; return x; }
    
    int neg = (x < 0);
    if (neg) x = -x;
    
    *exp = 0;
    while (x >= 1.0) { x *= 0.5; (*exp)++; }
    while (x < 0.5) { x *= 2.0; (*exp)--; }
    
    return neg ? -x : x;
}

/* ldexp: x * 2^exp */
double ldexp(double x, int exp) {
    if (x == 0.0 || isinf(x) || isnan(x)) return x;
    while (exp > 0) { x *= 2.0; exp--; }
    while (exp < 0) { x *= 0.5; exp++; }
    return x;
}

/* modf: split into integer and fractional parts */
double modf(double x, double *iptr) {
    double i = trunc(x);
    *iptr = i;
    return x - i;
}

/* scalbn: x * 2^n (FLT_RADIX = 2) */
double scalbn(double x, int n) {
    double res;
    __asm__ __volatile__("fildl %2; fldl %1; fscale; fstp %%st(1); fstpl %0" 
                         : "=m"(res) : "m"(x), "m"(n));
    return res;
}

/* nextafter: next representable value after x towards y */
double nextafter(double x, double y) {
    if (isnan(x) || isnan(y)) return NAN;
    if (x == y) return y;
    
    union { double d; uint64_t u; } u = { .d = x };
    
    if (x == 0.0) {
        /* Smallest subnormal */
        u.u = 1;
        return (y > 0) ? u.d : -u.d;
    }
    
    if ((x > 0) == (y > x)) {
        u.u++;
    } else {
        u.u--;
    }
    return u.d;
}

/* copysign: magnitude of x with sign of y */
double copysign(double x, double y) {
    union { double d; uint64_t u; } ux = { .d = x }, uy = { .d = y };
    ux.u = (ux.u & 0x7FFFFFFFFFFFFFFFULL) | (uy.u & 0x8000000000000000ULL);
    return ux.d;
}

/* Absolute value - actual implementation */
double fabs(double x) {
    double res;
    __asm__ __volatile__("fldl %1; fabs; fstpl %0" : "=m"(res) : "m"(x));
    return res;
}

/* Remainder functions */
double fmod(double x, double y) {
    if (y == 0.0) return NAN;
    double res;
    __asm__ __volatile__(
        "fldl %2\n\t"
        "fldl %1\n\t"
        "1: fprem\n\t"
        "fnstsw %%ax\n\t"
        "sahf\n\t"
        "jp 1b\n\t"
        "fstpl %0\n\t"
        "fstp %%st(0)"
        : "=m"(res) : "m"(x), "m"(y) : "ax", "cc");
    return res;
}

double remainder(double x, double y) {
    if (y == 0.0) return NAN;
    double res;
    __asm__ __volatile__(
        "fldl %2\n\t"
        "fldl %1\n\t"
        "1: fprem1\n\t"
        "fnstsw %%ax\n\t"
        "sahf\n\t"
        "jp 1b\n\t"
        "fstpl %0\n\t"
        "fstp %%st(0)"
        : "=m"(res) : "m"(x), "m"(y) : "ax", "cc");
    return res;
}

/* Min/Max - actual implementations */
double fmax(double x, double y) {
    if (isnan(x)) return y;
    if (isnan(y)) return x;
    return (x > y) ? x : y;
}

double fmin(double x, double y) {
    if (isnan(x)) return y;
    if (isnan(y)) return x;
    return (x < y) ? x : y;
}

/* Positive difference */
double fdim(double x, double y) {
    return (x > y) ? (x - y) : 0.0;
}

/* Rounding functions */
double ceil(double x) {
    int i = (int)x;
    return (x > i) ? (double)(i + 1) : (double)i;
}

double floor(double x) {
    int i = (int)x;
    return (x < i) ? (double)(i - 1) : (double)i;
}

double trunc(double x) {
    return (double)(int)x;
}

double round(double x) {
    return (x >= 0) ? floor(x + 0.5) : ceil(x - 0.5);
}

double rint(double x) {
    double res;
    __asm__ __volatile__("fldl %1; frndint; fstpl %0" : "=m"(res) : "m"(x));
    return res;
}

double nearbyint(double x) {
    return rint(x); // x87 frndint honors CW but doesn't necessarily raise inexact if masked
}

long lrint(double x) {
    long res;
    __asm__ __volatile__("fldl %1; fistpl %0" : "=m"(res) : "m"(x));
    return res;
}

long long llrint(double x) {
    long long res;
    __asm__ __volatile__("fldl %1; fistpq %0" : "=m"(res) : "m"(x));
    return res;
}

/* Float versions */
float sinf(float x) { return (float)sin(x); }
float cosf(float x) { return (float)cos(x); }
float tanf(float x) { return (float)tan(x); }
float sqrtf(float x) { return (float)sqrt(x); }
float powf(float x, float y) { return (float)pow(x, y); }
float fabsf(float x) { return (x < 0) ? -x : x; }
float fmodf(float x, float y) { return (float)fmod(x, y); }
float fmaxf(float x, float y) { return (float)fmax(x, y); }
float fminf(float x, float y) { return (float)fmin(x, y); }
float ceilf(float x) { return (float)ceil(x); }
float floorf(float x) { return (float)floor(x); }
float truncf(float x) { return (float)trunc(x); }
float roundf(float x) { return (float)round(x); }

/* Long double versions (same as double on i386) */
long double sinl(long double x) { return sin(x); }
long double cosl(long double x) { return cos(x); }
long double tanl(long double x) { return tan(x); }
long double sqrtl(long double x) { return sqrt(x); }
long double powl(long double x, long double y) { return pow(x, y); }
long double fabsl(long double x) { return (x < 0) ? -x : x; }
long double fmodl(long double x, long double y) { return fmod(x, y); }
long double fmaxl(long double x, long double y) { return fmax(x, y); }
long double fminl(long double x, long double y) { return fmin(x, y); }
long double ceill(long double x) { return ceil(x); }
long double floorl(long double x) { return floor(x); }
long double truncl(long double x) { return trunc(x); }
long double roundl(long double x) { return round(x); }
