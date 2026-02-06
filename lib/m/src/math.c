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
    /* Handle special cases */
    if (x == 0) return 1.0;
    if (x < -708) return 0.0;  /* Underflow */
    if (x > 709) return INFINITY;  /* Overflow */
    
    /* Reduce range: e^x = 2^k * e^r where r = x - k*ln(2) */
    int k = (int)(x * M_LOG2E);
    double r = x - k * M_LN2;
    
    /* Taylor series for e^r (|r| < ln(2)) */
    double term = 1.0, sum = 1.0;
    for (int i = 1; i < 30 && fabs(term) > 1e-15; i++) {
        term *= r / i;
        sum += term;
    }
    
    /* Multiply by 2^k */
    while (k > 0) { sum *= 2.0; k--; }
    while (k < 0) { sum *= 0.5; k++; }
    
    return sum;
}

/* exp2(x) = 2^x = e^(x * ln(2)) */
double exp2(double x) {
    return exp(x * M_LN2);
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
    if (x <= 0) return (x == 0) ? -INFINITY : NAN;
    if (x == 1) return 0.0;
    if (isinf(x)) return INFINITY;
    
    /* Reduce to [1, 2): x = m * 2^e, log(x) = log(m) + e*ln(2) */
    int e = 0;
    while (x >= 2.0) { x *= 0.5; e++; }
    while (x < 1.0) { x *= 2.0; e--; }
    
    /* Use series: log(x) = 2 * sum((t^(2k+1))/(2k+1)) where t = (x-1)/(x+1) */
    double t = (x - 1.0) / (x + 1.0);
    double t2 = t * t;
    double sum = t;
    double term = t;
    for (int k = 1; k < 50; k++) {
        term *= t2;
        sum += term / (2 * k + 1);
    }
    
    return 2.0 * sum + e * M_LN2;
}

/* log2(x) = log(x) / ln(2) */
double log2(double x) {
    return log(x) * M_LOG2E;
}

/* log10(x) = log(x) / ln(10) */
double log10(double x) {
    return log(x) / M_LN10;
}

/* log1p(x) = log(1+x), accurate for small x */
double log1p(double x) {
    if (fabs(x) < 1e-9) return x - 0.5 * x * x;  /* Taylor for small x */
    return log(1.0 + x);
}

/* pow(x, y) = x^y = e^(y * log(x)) */
double pow(double x, double y) {
    if (y == 0.0) return 1.0;
    if (x == 0.0) return (y > 0) ? 0.0 : INFINITY;
    if (x == 1.0) return 1.0;
    if (x < 0.0) {
        /* Handle negative base with integer exponent */
        int yi = (int)y;
        if (y != yi) return NAN;  /* Non-integer power of negative */
        double result = exp(y * log(-x));
        return (yi % 2) ? -result : result;
    }
    return exp(y * log(x));
}

/*
 * sqrt(x) - Square root using Newton-Raphson
 * x_{n+1} = 0.5 * (x_n + S/x_n)
 */
double sqrt(double x) {
    if (x < 0) return NAN;
    if (x == 0 || isinf(x)) return x;
    
    /* Initial guess */
    double guess = x * 0.5;
    if (guess == 0) guess = 1.0;
    
    /* Newton-Raphson iterations */
    for (int i = 0; i < 20; i++) {
        double next = 0.5 * (guess + x / guess);
        if (fabs(next - guess) < 1e-15 * fabs(guess)) break;
        guess = next;
    }
    
    return guess;
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
    /* Reduce to [-pi, pi] */
    while (x > M_PI) x -= 2.0 * M_PI;
    while (x < -M_PI) x += 2.0 * M_PI;
    
    /* Taylor series */
    double x2 = x * x;
    double term = x, sum = x;
    for (int i = 1; i < 20; i++) {
        term *= -x2 / ((2*i) * (2*i + 1));
        if (fabs(term) < 1e-15) break;
        sum += term;
    }
    return sum;
}

/*
 * cos(x) - Cosine using Taylor series
 * cos(x) = 1 - x^2/2! + x^4/4! - ...
 */
double cos(double x) {
    /* Reduce to [-pi, pi] */
    while (x > M_PI) x -= 2.0 * M_PI;
    while (x < -M_PI) x += 2.0 * M_PI;
    
    /* Taylor series */
    double x2 = x * x;
    double term = 1.0, sum = 1.0;
    for (int i = 1; i < 20; i++) {
        term *= -x2 / ((2*i - 1) * (2*i));
        if (fabs(term) < 1e-15) break;
        sum += term;
    }
    return sum;
}

/* tan(x) = sin(x) / cos(x) */
double tan(double x) {
    double c = cos(x);
    if (fabs(c) < 1e-15) return (x > 0) ? INFINITY : -INFINITY;
    return sin(x) / c;
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
    if (x > 0) return atan(y / x);
    if (x < 0) {
        if (y >= 0) return atan(y / x) + M_PI;
        return atan(y / x) - M_PI;
    }
    /* x == 0 */
    if (y > 0) return M_PI_2;
    if (y < 0) return -M_PI_2;
    return 0.0;  /* y == 0 too */
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
    return ldexp(x, n);
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
    return (x < 0) ? -x : x;
}

/* Remainder functions */
double fmod(double x, double y) {
    if (y == 0.0) return NAN;
    return x - (int)(x / y) * y;
}

double remainder(double x, double y) {
    if (y == 0.0) return NAN;
    double n = x / y;
    double q = (n >= 0) ? (int)(n + 0.5) : (int)(n - 0.5);
    return x - q * y;
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
    /* Round to nearest integer, ties to even (banker's rounding) */
    double n = floor(x + 0.5);
    /* If exactly halfway, round to even */
    if (x + 0.5 == n && (int)n % 2 != 0) {
        n -= 1.0;
    }
    return n;
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
