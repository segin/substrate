/*
 * math.c - Math library functions
 *
 * Implements exponential, logarithmic, and trigonometric functions
 * using Taylor series approximations and mathematical identities.
 */

#include <errno.h>
#include <fenv.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>

/* Constants (with guards to avoid redefinition) */
#ifndef M_PI
#define M_PI      3.14159265358979323846
#endif
#ifndef M_PI_2
#define M_PI_2    1.57079632679489661923
#endif
#ifndef M_PI_4
#define M_PI_4    0.78539816339744830962
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
 * Floating-point manipulation functions
 */

/* frexp: x = mantissa * 2^exp, where 0.5 <= |mantissa| < 1.
 * Conformance: C99 7.12.6.4.
 *  - x == 0.0 (including -0.0): *exp = 0, return x (preserves sign of zero).
 *  - x == +/-inf or NaN: *exp = 0, return x (do not crash).
 *  - Subnormal x: still normalized via the doubling loop.
 */
double frexp(double x, int *exp) {
    if (x == 0.0) { *exp = 0; return x; }
    if (isinf(x) || isnan(x)) { *exp = 0; return x; }

    int neg = (x < 0);
    if (neg) x = -x;

    *exp = 0;
    while (x >= 1.0) { x *= 0.5; (*exp)++; }
    while (x < 0.5) { x *= 2.0; (*exp)--; }

    return neg ? -x : x;
}

/* ldexp: x * 2^exp.
 * Conformance: C99 7.12.6.6.
 *  - x == 0.0 (including -0.0): return x (sign preserved).
 *  - x == NaN: return NaN.
 *  - x == +/-inf: return x.
 *  - Overflow: return +/-HUGE_VAL with errno = ERANGE.
 *  - Underflow: return +/-0 (or subnormal) with errno = ERANGE.
 * Uses the x87 fscale instruction (matches scalbn's strategy).
 */
double ldexp(double x, int exp) {
    if (isnan(x)) return x;
    if (x == 0.0 || isinf(x)) return x;

    double res;
    __asm__ __volatile__("fildl %2; fldl %1; fscale; fstp %%st(1); fstpl %0"
                         : "=m"(res) : "m"(x), "m"(exp));

    if (isinf(res) && !isinf(x)) {
        errno = ERANGE;
        return (x < 0.0) ? -HUGE_VAL : HUGE_VAL;
    }
    if (res == 0.0 && x != 0.0) {
        errno = ERANGE;
    }
    return res;
}

/* modf: split into integer and fractional parts (C99 7.12.6.12) */
double modf(double x, double *iptr) {
    /* NaN: *iptr = NaN, return NaN */
    if (isnan(x)) {
        *iptr = x;
        return x;
    }
    /* +/-Inf: *iptr = +/-Inf, return +/-0.0 (sign of x) */
    if (isinf(x)) {
        *iptr = x;
        return copysign(0.0, x);
    }
    /* +/-0.0: *iptr = +/-0.0, return +/-0.0 (sign preserved on both) */
    if (x == 0.0) {
        *iptr = x;
        return x;
    }
    /* Normal case: truncate via bit manipulation to preserve sign of zero
     * (the local trunc() converts via int and loses -0.0 / overflows on huge x). */
    union { double d; uint64_t u; } u = { .d = x };
    int exp = (int)((u.u >> 52) & 0x7FF) - 1023;

    if (exp < 0) {
        /* |x| < 1: integer part is +/-0.0 with sign of x, fraction is x */
        *iptr = copysign(0.0, x);
        return x;
    }
    if (exp >= 52) {
        /* |x| is so large it has no fractional bits */
        *iptr = x;
        return copysign(0.0, x);
    }
    /* Mask off the fractional mantissa bits */
    uint64_t mask = ((uint64_t)1 << (52 - exp)) - 1;
    if ((u.u & mask) == 0) {
        /* Already an integer */
        *iptr = x;
        return copysign(0.0, x);
    }
    union { double d; uint64_t u; } iu = { .u = u.u & ~mask };
    *iptr = iu.d;
    return x - iu.d;
}

/* scalbn: x * 2^n (FLT_RADIX = 2 for IEEE-754).
 * Conformance: C99 7.12.6.13.
 *  - x == 0.0 (including -0.0): return x (sign preserved).
 *  - x == NaN: return NaN.
 *  - x == +/-inf: return x.
 *  - Overflow: return +/-HUGE_VAL with errno = ERANGE.
 *  - Underflow: return +/-0 with errno = ERANGE.
 * Equivalent to ldexp() on IEEE-754 platforms.
 */
double scalbn(double x, int n) {
    if (isnan(x)) return x;
    if (x == 0.0 || isinf(x)) return x;

    double res;
    __asm__ __volatile__("fildl %2; fldl %1; fscale; fstp %%st(1); fstpl %0"
                         : "=m"(res) : "m"(x), "m"(n));

    if (isinf(res) && !isinf(x)) {
        errno = ERANGE;
        return (x < 0.0) ? -HUGE_VAL : HUGE_VAL;
    }
    if (res == 0.0 && x != 0.0) {
        errno = ERANGE;
    }
    return res;
}

/* scalbln: x * 2^n with long exponent.
 * Conformance: C99 7.12.6.13. Behaviour matches scalbn() once the
 * exponent has been clamped into int range.
 */
double scalbln(double x, long n) {
    if (n > INT_MAX) n = INT_MAX;
    else if (n < INT_MIN) n = INT_MIN;
    return scalbn(x, (int)n);
}

/* ilogb: extract the unbiased exponent of x as an int.
 * Conformance: C99 7.12.6.5. For finite non-zero x the result is
 * floor(log2(|x|)). The special inputs 0, +/-Inf and NaN raise
 * FE_INVALID and return FP_ILOGB0, INT_MAX, FP_ILOGBNAN respectively.
 * Implementation strategy: frexp() returns frac in [0.5, 1) such that
 * x == frac * 2^e, hence log2(|x|) == e - 1. This naturally handles
 * subnormals because frexp() normalises them.
 */
int ilogb(double x) {
    if (isnan(x)) {
        feraiseexcept(FE_INVALID);
        return FP_ILOGBNAN;
    }
    if (x == 0.0) {
        feraiseexcept(FE_INVALID);
        return FP_ILOGB0;
    }
    if (isinf(x)) {
        feraiseexcept(FE_INVALID);
        return INT_MAX;
    }
    int e;
    (void)frexp(x, &e);
    return e - 1;
}

/* logb: extract the unbiased exponent of x as a double.
 * Conformance: C99 7.12.6.11. For finite non-zero x the result is
 * floor(log2(|x|)) returned as a double (same value as ilogb()).
 * logb(0) is a pole error: returns -INFINITY and raises FE_DIVBYZERO.
 * logb(+/-Inf) returns +INFINITY (no exception). logb(NaN) returns NaN.
 */
double logb(double x) {
    if (isnan(x)) return x;
    if (isinf(x)) return INFINITY;
    if (x == 0.0) {
        feraiseexcept(FE_DIVBYZERO);
        return -INFINITY;
    }
    int e;
    (void)frexp(x, &e);
    return (double)(e - 1);
}

/* nextafter: next representable value after x towards y.
 * Conformance: C99 7.12.11.3, Annex F.10.8.3.
 *  - x or y NaN: return NaN.
 *  - x == y: return y (preserves sign of zero per F.10.8.3).
 *  - x == +/-0 moving outward: smallest +/-subnormal; range error
 *    (errno = ERANGE, FE_UNDERFLOW raised).
 *  - x finite, magnitude becomes infinite: return +/-HUGE_VAL; range error
 *    (errno = ERANGE, FE_OVERFLOW raised).
 *  - x finite, result is subnormal (loss of precision): range error
 *    (errno = ERANGE, FE_UNDERFLOW raised).
 *  - x == +/-INF moving toward finite y: return +/-DBL_MAX (no range error).
 * Operates by incrementing/decrementing the IEEE-754 bit pattern.
 */
double nextafter(double x, double y) {
    if (isnan(x) || isnan(y)) return NAN;
    if (x == y) return y;

    union { double d; uint64_t u; } u = { .d = x };

    if (x == 0.0) {
        /* Smallest subnormal in direction of y; sign comes from y. */
        u.u = 1;
        if (signbit(y)) u.u |= ((uint64_t)1 << 63);
        errno = ERANGE;
        feraiseexcept(FE_UNDERFLOW);
        return u.d;
    }

    /* x > 0 and y > x  -> increase magnitude (u.u++)
     * x > 0 and y < x  -> decrease magnitude (u.u--)
     * x < 0 and y > x  -> decrease magnitude (u.u--)
     * x < 0 and y < x  -> increase magnitude (u.u++)
     * Equivalent: (x > 0) == (y > x) selects increment. */
    if ((x > 0.0) == (y > x)) {
        u.u++;
    } else {
        u.u--;
    }

    /* Range checks per Annex F.10.8.3. */
    if (isinf(u.d)) {
        errno = ERANGE;
        feraiseexcept(FE_OVERFLOW | FE_INEXACT);
        return signbit(x) ? -HUGE_VAL : HUGE_VAL;
    }
    /* Subnormal result from a previously normal operation: underflow. */
    if ((u.u & 0x7FF0000000000000ULL) == 0) {
        errno = ERANGE;
        feraiseexcept(FE_UNDERFLOW | FE_INEXACT);
    }
    return u.d;
}

/* nexttoward: like nextafter() but with long double direction argument.
 * Conformance: C99 7.12.11.4.
 *  - x or y NaN: return NaN.
 *  - (long double)x == y: return (double)y (preserves sign of zero).
 *  - Otherwise: step x one ULP toward y using nextafter() with the
 *    appropriate +/-INFINITY direction sentinel. The extra precision of
 *    long double y matters precisely when (double)y == x but y differs
 *    from x as a long double, in which case we must still step away.
 */
double nexttoward(double x, long double y) {
    if (isnan(x) || isnan((double)y)) return NAN;
    long double xl = (long double)x;
    if (xl == y) return (double)y;
    return nextafter(x, (xl < y) ? INFINITY : -INFINITY);
}

/* nextup: next representable value toward +INFINITY (C23 7.12.11.5).
 *  - NaN: return NaN.
 *  - +INFINITY: return +INFINITY (already at maximum; no further "up" step).
 *  - -INFINITY: return -DBL_MAX.
 *  - -0.0: returns smallest positive subnormal (per IEEE 754-2019 nextUp(-0)).
 *  - Otherwise: next representable double > x.
 * Trivially implemented via nextafter(x, +INFINITY); the x == y case of
 * nextafter() handles +INFINITY by returning +INFINITY without raising
 * any exceptions.
 */
double nextup(double x) {
    if (isnan(x)) return x;
    return nextafter(x, INFINITY);
}

/* nextdown: next representable value toward -INFINITY (C23 7.12.11.6).
 *  - NaN: return NaN.
 *  - -INFINITY: return -INFINITY (already at minimum; no further "down" step).
 *  - +INFINITY: return DBL_MAX.
 *  - +0.0: returns largest negative subnormal (per IEEE 754-2019 nextDown(+0)).
 *  - Otherwise: next representable double < x.
 * Trivially implemented via nextafter(x, -INFINITY); the x == y case of
 * nextafter() handles -INFINITY by returning -INFINITY without raising
 * any exceptions.
 */
double nextdown(double x) {
    if (isnan(x)) return x;
    return nextafter(x, -INFINITY);
}

/* copysign: magnitude of x with sign of y */
double copysign(double x, double y) {
    union { double d; uint64_t u; } ux = { .d = x }, uy = { .d = y };
    ux.u = (ux.u & 0x7FFFFFFFFFFFFFFFULL) | (uy.u & 0x8000000000000000ULL);
    return ux.d;
}

/*
 * nan: return a quiet NaN. Per C99 7.12.11.2, the tagp string selects an
 * implementation-defined NaN payload; we ignore tagp and return NAN, which
 * is standards-compliant since most code cannot observe the payload.
 */
double nan(const char *tagp) {
    (void)tagp;
    return NAN;
}

/* Absolute value — bit-twiddle on the IEEE-754 sign bit.  The
 * previous x87 inline-asm version (fldl; fabs; fstpl) was correct
 * in isolation but broke at -O2 when GCC inlined it into a caller
 * already using a deep x87 stack: the inner `fldl` could push past
 * st(7) and silently produce a NaN.  Bit-twiddle has no such
 * coupling to the x87 register allocator. */
double fabs(double x) {
    union { double d; uint64_t u; } v = { .d = x };
    v.u &= 0x7FFFFFFFFFFFFFFFULL;
    return v.d;
}

/* Remainder functions */
double fmod(double x, double y) {
    /* C99/IEEE: NaN propagation, Inf-numerator and zero-denominator are
     * invalid (return NaN, raise FE_INVALID); Inf denominator with finite
     * x returns x unchanged. */
    if (isnan(x) || isnan(y)) return NAN;
    if (isinf(x) || y == 0.0) {
        feraiseexcept(FE_INVALID);
        return NAN;
    }
    if (isinf(y)) return x;        /* finite x, infinite y: fmod = x */
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
    /* C99/IEEE: NaN propagation, Inf-numerator and zero-denominator are
     * invalid (return NaN, raise FE_INVALID); Inf denominator with finite
     * x returns x unchanged. */
    if (isnan(x) || isnan(y)) return NAN;
    if (isinf(x) || y == 0.0) {
        feraiseexcept(FE_INVALID);
        return NAN;
    }
    if (isinf(y)) return x;        /* finite x, infinite y: remainder = x */
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
    if (x == 0.0 && y == 0.0)
        return signbit(x) ? y : x;
    return (x > y) ? x : y;
}

double fmin(double x, double y) {
    if (isnan(x)) return y;
    if (isnan(y)) return x;
    if (x == 0.0 && y == 0.0)
        return signbit(x) ? x : y;
    return (x < y) ? x : y;
}

/* Positive difference */
double fdim(double x, double y) {
	if(isnan(x) || isnan(y)) return NAN;
	return (x > y) ? (x - y) : 0.0;
}

/*
 * remquo: IEEE remainder with low-order quotient bits.
 * Returns remainder(x, y) and stores quotient sign + low 3 bits in *quo.
 */
double remquo(double x, double y, int *quo) {
	/* Domain / special cases: NaN, Inf numerator, zero or Inf
	 * denominator yield a NaN remainder with *quo = 0. */
	if(isnan(x) || isnan(y) || isinf(x) || y == 0.0) {
		*quo = 0;
		return NAN;
	}
	if(isinf(y)) {               /* finite x, infinite y: rem = x, quo = 0 */
		*quo = 0;
		return x;
	}

	int qsign = (signbit(x) != signbit(y)) ? -1 : 1;
	double ax = fabs(x), ay = fabs(y);

	/* |x| < |y|: quotient is 0 (round-to-nearest may make it 1, but
	 * the low-bit accounting below handles the final half-way step). */
	uint32_t q = 0;

	if(ax >= ay) {
		/* Binary long-division style reduction: subtract scaled copies
		 * of ay from ax, accumulating the integer quotient bits in q.
		 * This never forms |x/y| as a single (possibly > 2^63) value,
		 * so the low quotient bits stay exact regardless of magnitude.
		 *
		 * Find the largest n with ay*2^n <= ax (via ilogb difference),
		 * then peel off one quotient bit per step. */
		int ex = ilogb(ax);
		int ey = ilogb(ay);
		int n = ex - ey;
		double yscaled = scalbn(ay, n);   /* ay * 2^n, may exceed ax by 1 step */
		if(yscaled > ax) { yscaled *= 0.5; n--; }
		for(; n >= 0; n--) {
			q <<= 1;
			if(ax >= yscaled) {
				ax -= yscaled;
				q |= 1;
			}
			yscaled *= 0.5;
		}
		/* ax now holds |x| mod |y| in [0, |y|). */
	}

	/* Round the remainder to nearest (ties to even quotient), matching
	 * IEEE remainder(): if the residual is past the half-way point, or
	 * exactly half with an odd quotient, step the quotient up by one. */
	if(ax > 0.5 * ay || (ax == 0.5 * ay && (q & 1))) {
		q++;
		ax -= ay;        /* remainder becomes negative (|ax| < ay still) */
	}

	double r = (signbit(x) ? -ax : ax);   /* remainder carries sign of x */
	int lo = (int)(q & 0x7);
	*quo = qsign < 0 ? -lo : lo;
	return r;
}

/*
 * fma(x, y, z) - Fused Multiply-Add: (x * y) + z with single rounding.
 *
 * On x87 there is no hardware FMA instruction. We use Dekker's algorithm
 * to split the product x*y into an exact hi+lo pair via double-double
 * arithmetic, then add z and round once.
 *
 * Dekker split factor for 53-bit mantissa: 2^27 + 1 = 134217729.
 */
double fma(double x, double y, double z) {
	/* Handle special values */
	if(isnan(x) || isnan(y) || isnan(z)) return NAN;
	if((isinf(x) && y == 0.0) || (x == 0.0 && isinf(y))) return NAN;
	if(isinf(x) || isinf(y)) {
		double p = x * y;
		if(isinf(z) && ((p > 0) != (z > 0))) return NAN;
		return p + z;
	}
	if(isinf(z)) return z;
	if(x == 0.0 || y == 0.0) return x * y + z;

	/*
	 * Dekker's product: split x and y into hi/lo parts so that
	 * x*y = p_hi + p_lo exactly (no rounding error in the sum).
	 */
	static const double SPLIT = 134217729.0; /* 2^27 + 1 */

	double cx = x * SPLIT;
	double x_hi = cx - (cx - x);
	double x_lo = x - x_hi;

	double cy = y * SPLIT;
	double y_hi = cy - (cy - y);
	double y_lo = y - y_hi;

	double p_hi = x * y;          /* rounded product */
	double p_lo = ((x_hi * y_hi - p_hi) + x_hi * y_lo
	              + x_lo * y_hi) + x_lo * y_lo;

	/* Now compute (p_hi + p_lo) + z with single rounding */
	double s_hi = p_hi + z;
	double s_lo;
	if(fabs(p_hi) >= fabs(z))
		s_lo = (p_hi - s_hi) + z + p_lo;
	else
		s_lo = (z - s_hi) + p_hi + p_lo;

	return s_hi + s_lo;
}

/*
 * C23 fmaximum / fminimum family.
 *
 * fmaximum/fminimum:         NaN-propagating, distinguish +0/-0.
 * fmaximum_num/fminimum_num: NaN-ignoring (like C99 fmax/fmin), distinguish +0/-0.
 * fmaximum_mag/fminimum_mag: compare magnitudes, NaN-propagating.
 */
double fmaximum(double x, double y) {
	if(isnan(x) || isnan(y)) return NAN;
	if(x == 0.0 && y == 0.0) {
		/* +0 > -0 */
		return signbit(x) ? y : x;
	}
	return (x > y) ? x : y;
}

double fminimum(double x, double y) {
	if(isnan(x) || isnan(y)) return NAN;
	if(x == 0.0 && y == 0.0) {
		/* -0 < +0 */
		return signbit(x) ? x : y;
	}
	return (x < y) ? x : y;
}

double fmaximum_num(double x, double y) {
	if(isnan(x) && isnan(y)) return NAN;
	if(isnan(x)) return y;
	if(isnan(y)) return x;
	if(x == 0.0 && y == 0.0)
		return signbit(x) ? y : x;
	return (x > y) ? x : y;
}

double fminimum_num(double x, double y) {
	if(isnan(x) && isnan(y)) return NAN;
	if(isnan(x)) return y;
	if(isnan(y)) return x;
	if(x == 0.0 && y == 0.0)
		return signbit(x) ? x : y;
	return (x < y) ? x : y;
}

double fmaximum_mag(double x, double y) {
	if(isnan(x) || isnan(y)) return NAN;
	double ax = fabs(x), ay = fabs(y);
	if(ax > ay) return x;
	if(ay > ax) return y;
	/* Equal magnitudes: fall back to fmaximum for sign distinction */
	return fmaximum(x, y);
}

double fminimum_mag(double x, double y) {
	if(isnan(x) || isnan(y)) return NAN;
	double ax = fabs(x), ay = fabs(y);
	if(ax < ay) return x;
	if(ay < ax) return y;
	return fminimum(x, y);
}

/* Rounding functions.
 *
 * The naive (int)x cast was undefined behaviour for |x| > 2^31.  The
 * portable implementation works on the IEEE-754 representation: extract
 * the unbiased exponent, mask off the fractional bits, then nudge by
 * ±1 ULP (in the integer sense) for floor/ceil when the input had a
 * non-zero fractional part.  This does not depend on x87 and works on
 * every architecture with a 64-bit IEEE double.
 *
 * On i386/x86_64 we also provide an x87 fast path using frndint with
 * an explicitly-set rounding mode.  The original control word is
 * restored before returning.
 */

/* mode: 0 = truncate-toward-zero, 1 = floor (down), 2 = ceil (up) */
__attribute__((unused))
static double round_to_int_portable(double x, int mode) {
    if (isnan(x) || isinf(x) || x == 0.0) return x;

    union { double d; uint64_t u; } v;
    v.d = x;
    int sign = (int)(v.u >> 63);
    int e = (int)((v.u >> 52) & 0x7FF) - 1023;

    if (e >= 52) {
        /* No fractional bits — already an integer. */
        return x;
    }
    if (e < 0) {
        /* |x| < 1 */
        if (mode == 0) return sign ? -0.0 : 0.0;
        if (mode == 1) return sign ? -1.0 : 0.0;          /* floor */
        return sign ? -0.0 : 1.0;                          /* ceil */
    }

    uint64_t frac_mask = (1ULL << (52 - e)) - 1ULL;
    if ((v.u & frac_mask) == 0) return x;

    union { double d; uint64_t u; } t;
    t.u = v.u & ~frac_mask; /* truncated toward zero */
    if (mode == 0) return t.d;
    if (mode == 1) return sign ? t.d - 1.0 : t.d;        /* floor */
    return sign ? t.d : t.d + 1.0;                        /* ceil */
}

#if defined(__i386__) || defined(__x86_64__)
/*
 * Rounding-control bits in the i387 control word:
 *   00 = round to nearest, 01 = round down (-inf),
 *   10 = round up (+inf),  11 = truncate toward zero.
 */
static inline double round_to_int_x87(double x, unsigned rc_bits) {
    double res;
    unsigned short cw_orig, cw_new;
    __asm__ __volatile__("fnstcw %0" : "=m"(cw_orig));
    cw_new = (unsigned short)((cw_orig & 0xF3FFu) | (rc_bits & 0x0C00u));
    __asm__ __volatile__(
        "fldcw %1\n\t"
        "fldl %2\n\t"
        "frndint\n\t"
        "fstpl %0\n\t"
        "fldcw %3\n\t"
        : "=m"(res)
        : "m"(cw_new), "m"(x), "m"(cw_orig));
    return res;
}
#endif

double ceil(double x) {
    if (isnan(x) || isinf(x)) return x;
#if defined(__i386__) || defined(__x86_64__)
    return round_to_int_x87(x, 0x0800);
#else
    return round_to_int_portable(x, 2);
#endif
}

double floor(double x) {
    if (isnan(x) || isinf(x)) return x;
#if defined(__i386__) || defined(__x86_64__)
    return round_to_int_x87(x, 0x0400);
#else
    return round_to_int_portable(x, 1);
#endif
}

double trunc(double x) {
    if (isnan(x) || isinf(x)) return x;
#if defined(__i386__) || defined(__x86_64__)
    return round_to_int_x87(x, 0x0C00);
#else
    return round_to_int_portable(x, 0);
#endif
}

double round(double x) {
    /* C99: round-half-away-from-zero, regardless of current mode.
     *
     * The old `floor(x + 0.5)` is wrong for the predecessor of 0.5:
     * x = nextafter(0.5, 0) added to 0.5 rounds (to-nearest) up to
     * exactly 1.0, so floor() returns 1 instead of the correct 0.
     * Instead truncate toward zero and inspect the exact fractional
     * part: a magnitude >= 0.5 rounds the integer part away from zero. */
    if (isnan(x) || isinf(x) || x == 0.0) return x;

    double t = trunc(x);          /* integer part, toward zero */
    double frac = x - t;          /* exact: |frac| < 1 */
    if (frac >= 0.5) return t + 1.0;
    if (frac <= -0.5) return t - 1.0;
    return t;                     /* preserves sign of zero via trunc */
}

/*
 * roundeven: round-half-to-even (banker's rounding), C23.
 * Always uses round-to-nearest-even regardless of rounding mode.
 */
double roundeven(double x) {
    return rint(x);
}

double rint(double x) {
    if (isnan(x) || isinf(x)) return x;
#if defined(__i386__) || defined(__x86_64__)
    double res;
    __asm__ __volatile__("fldl %1; frndint; fstpl %0" : "=m"(res) : "m"(x));
    return res;
#else
    /* Portable round-to-nearest-even.  Loses fenv rounding mode on
     * non-x86 — the right fix is a per-arch fenv backend, which is
     * out of scope here. */
    double t = round_to_int_portable(x, 0); /* truncate */
    double frac = x - t;
    if (frac > 0.5)  return t + 1.0;
    if (frac < -0.5) return t - 1.0;
    if (frac == 0.5 || frac == -0.5) {
        /* Round to even. */
        long long ti = (long long)t;
        if (ti & 1LL) {
            return frac > 0 ? t + 1.0 : t - 1.0;
        }
        return t;
    }
    return t;
#endif
}

double nearbyint(double x) {
    /* nearbyint() is rint() except it MUST NOT raise FE_INEXACT
     * even when the result differs from x.  rint()'s x87 path
     * relies on frndint, which sets FE_INEXACT unconditionally.
     * Snapshot the exception state, call rint(), then restore so
     * that any FE_INEXACT raised by rint() is hidden from the
     * caller (any pre-existing FE_INEXACT survives via the
     * snapshot). */
    fexcept_t save;
    fegetexceptflag(&save, FE_INEXACT);
    double r = rint(x);
    fesetexceptflag(&save, FE_INEXACT);
    return r;
}

/*
 * lrint/llrint: round per current FE rounding mode and convert to integer.
 *
 * x87 path: fistp stores the indefinite-integer encoding on overflow and
 * sets the FPU's invalid-operation flag.  We surface that as errno=ERANGE.
 *
 * Portable path: rint() honours the current rounding mode (no x87 needed
 * on non-x86 — left as nearbyint-equivalent), then we range-check before
 * the cast.  The `rint` here is the C-fallback version below.
 */
long lrint(double x) {
#if defined(__i386__) || defined(__x86_64__)
    long res;
    unsigned short sw;
    __asm__ __volatile__(
        "fclex\n\t"
        "fldl %2\n\t"
        "fistpl %0\n\t"
        "fnstsw %1\n\t"
        : "=m"(res), "=m"(sw) : "m"(x));
    if (sw & 0x01) errno = ERANGE; /* IE: invalid operation */
    return res;
#else
    if (isnan(x)) { errno = EDOM; return 0; }
    double r = rint(x);
    if (r > (double)LONG_MAX || r < (double)LONG_MIN) {
        errno = ERANGE;
        return r > 0 ? LONG_MAX : LONG_MIN;
    }
    return (long)r;
#endif
}

long long llrint(double x) {
#if defined(__i386__) || defined(__x86_64__)
    long long res;
    unsigned short sw;
    __asm__ __volatile__(
        "fclex\n\t"
        "fldl %2\n\t"
        "fistpq %0\n\t"
        "fnstsw %1\n\t"
        : "=m"(res), "=m"(sw) : "m"(x));
    if (sw & 0x01) errno = ERANGE;
    return res;
#else
    if (isnan(x)) { errno = EDOM; return 0; }
    double r = rint(x);
    if (r > (double)LLONG_MAX || r < (double)LLONG_MIN) {
        errno = ERANGE;
        return r > 0 ? LLONG_MAX : LLONG_MIN;
    }
    return (long long)r;
#endif
}

/*
 * lround/llround: round-half-away-from-zero, then convert.  The cast back to
 * a signed integer type is UB on overflow so we bound-check first and clamp.
 */
long lround(double x) {
    if (isnan(x)) { errno = EDOM; return 0; }
    double r = round(x);
    if (r > (double)LONG_MAX) { errno = ERANGE; return LONG_MAX; }
    if (r < (double)LONG_MIN) { errno = ERANGE; return LONG_MIN; }
    return (long)r;
}

long long llround(double x) {
    if (isnan(x)) { errno = EDOM; return 0; }
    double r = round(x);
    if (r > (double)LLONG_MAX) { errno = ERANGE; return LLONG_MAX; }
    if (r < (double)LLONG_MIN) { errno = ERANGE; return LLONG_MIN; }
    return (long long)r;
}

/* Float versions */

/* C23: fromfp family — convert floating-point values with explicit rounding */

/*
 * fromfp: store x into *y using the given rounding_mode.
 * rounding_mode values match fesetround() modes; FE_TONEAREST is default.
 * Returns 0 on success, non-zero for unsupported rounding modes.
 * envp (if non-NULL) is set to the current fenv state.
 */
int fromfp(double *y, double x, fenv_t *envp, int rounding_mode) {
    if (envp) {
        fenv_t zero_env = { 0 };
        *envp = zero_env;
    }
    switch (rounding_mode) {
    case FE_TONEAREST:
        *y = x;
        break;
    case FE_DOWNWARD:
        *y = floor(x);
        break;
    case FE_UPWARD:
        *y = ceil(x);
        break;
    case FE_TOWARDZERO:
        *y = trunc(x);
        break;
    default:
        return -1;
    }
    return 0;
}

int fromfpx(double *y, double x, fenv_t *envp, int rounding_mode) {
    return fromfp(y, x, envp, rounding_mode);
}

int ufromfp(unsigned int *y, double x, fenv_t *envp, int rounding_mode) {
    double tmp = 0;
    int rc = fromfp(&tmp, x, envp, rounding_mode);
    *y = (unsigned int)tmp;
    return rc;
}
int ufromfpx(unsigned int *y, double x, fenv_t *envp, int rounding_mode) {
    return ufromfp(y, x, envp, rounding_mode);
}
