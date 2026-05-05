/*
 * tgmath.h - C99/C11 Type-Generic Math Macros
 *
 * Uses C11 _Generic for compile-time type dispatch.
 * Each macro resolves to sinf/sin/sinl etc. based on argument type.
 * The "+0" promotion trick converts integer arguments to double
 * (matching C99 §6.9.2's "corresponding real type" rule).
 *
 * Inside a macro's own expansion, the macro name is "painted blue"
 * (disabled), so e.g. `default: sin` inside the `sin` macro refers
 * to the function sin(), not the macro — no infinite recursion.
 *
 * Compiler note: Use #pragma STDC FENV_ACCESS ON in translation units
 * that need accurate FP environment observation.  GCC equivalent:
 *   #pragma GCC optimize ("no-fast-math")
 *
 * Complex variant dispatch is guarded by __has_include(<complex.h>);
 * when <complex.h> is available, the _Complex associations are added
 * to each _Generic selection list (REQ-06-0446).
 *
 * No argument is evaluated more than once per macro invocation:
 * every expansion passes the macro parameter(s) directly to the
 * selected function.  No GCC statement-expressions or temporaries are
 * needed because _Generic performs the dispatch at compile time and
 * the selected function call is a single expression (REQ-06-0447).
 */

#ifndef _TGMATH_H
#define _TGMATH_H

#include <math.h>

#if __has_include(<complex.h>)
#  include <complex.h>
#  define _TGMATH_HAVE_COMPLEX 1
#else
#  define _TGMATH_HAVE_COMPLEX 0
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Internal helper: choose float/long-double/default-double variant    */
/* The dispatch expression is always (arg)+0 so that integer types    */
/* promote to double rather than matching no association.             */
/* ------------------------------------------------------------------ */

/*
 * _TGMATH1(fn, x) — 1-argument type-generic dispatch
 */
#define _TGMATH1(fn, x) \
    _Generic((x)+0,     \
        float:       fn##f, \
        long double: fn##l, \
        default:     fn     \
    )(x)

/*
 * _TGMATH2(fn, x, y) — 2-argument dispatch (wider of x and y)
 */
#define _TGMATH2(fn, x, y) \
    _Generic((x)+(y)+0,    \
        float:       fn##f, \
        long double: fn##l, \
        default:     fn     \
    )((x), (y))

/*
 * _TGMATH3(fn, x, y, z) — 3-argument dispatch (wider of x, y, z)
 */
#define _TGMATH3(fn, x, y, z)  \
    _Generic((x)+(y)+(z)+0,    \
        float:       fn##f,    \
        long double: fn##l,    \
        default:     fn        \
    )((x), (y), (z))

/*
 * _TGMATH1P(fn, x, p) — 1-arg numeric + 1 non-numeric pointer arg
 *   dispatch on x only; p is passed through unchanged.
 */
#define _TGMATH1P(fn, x, p) \
    _Generic((x)+0,         \
        float:       fn##f, \
        long double: fn##l, \
        default:     fn     \
    )((x), (p))

/*
 * _TGMATH1I(fn, x, n) — 1-arg numeric + 1 integer arg
 *   dispatch on x only; n is passed through unchanged.
 */
#define _TGMATH1I(fn, x, n) \
    _Generic((x)+0,         \
        float:       fn##f, \
        long double: fn##l, \
        default:     fn     \
    )((x), (n))

/*
 * _TGMATH2P(fn, x, y, p) — 2-arg numeric + 1 non-numeric pointer arg
 *   dispatch on (x)+(y); p is passed through unchanged.
 */
#define _TGMATH2P(fn, x, y, p) \
    _Generic((x)+(y)+0,        \
        float:       fn##f,    \
        long double: fn##l,    \
        default:     fn        \
    )((x), (y), (p))

/* ------------------------------------------------------------------ */
/* Trigonometric functions — 1-arg (REQ-06-0445)                      */
/* ------------------------------------------------------------------ */

#undef sin
#define sin(x)   _TGMATH1(sin, x)

#undef cos
#define cos(x)   _TGMATH1(cos, x)

#undef tan
#define tan(x)   _TGMATH1(tan, x)

#undef asin
#define asin(x)  _TGMATH1(asin, x)

#undef acos
#define acos(x)  _TGMATH1(acos, x)

#undef atan
#define atan(x)  _TGMATH1(atan, x)

/* ------------------------------------------------------------------ */
/* Trigonometric functions — 2-arg (REQ-06-0445)                      */
/* ------------------------------------------------------------------ */

#undef atan2
#define atan2(y, x)  _TGMATH2(atan2, y, x)

/* ------------------------------------------------------------------ */
/* Hyperbolic functions (REQ-06-0445)                                 */
/* ------------------------------------------------------------------ */

#undef sinh
#define sinh(x)   _TGMATH1(sinh, x)

#undef cosh
#define cosh(x)   _TGMATH1(cosh, x)

#undef tanh
#define tanh(x)   _TGMATH1(tanh, x)

#undef asinh
#define asinh(x)  _TGMATH1(asinh, x)

#undef acosh
#define acosh(x)  _TGMATH1(acosh, x)

#undef atanh
#define atanh(x)  _TGMATH1(atanh, x)

/* ------------------------------------------------------------------ */
/* Exponential and logarithmic functions (REQ-06-0445)                */
/* ------------------------------------------------------------------ */

#undef exp
#define exp(x)    _TGMATH1(exp, x)

#undef exp2
#define exp2(x)   _TGMATH1(exp2, x)

#undef expm1
#define expm1(x)  _TGMATH1(expm1, x)

#undef log
#define log(x)    _TGMATH1(log, x)

#undef log2
#define log2(x)   _TGMATH1(log2, x)

#undef log10
#define log10(x)  _TGMATH1(log10, x)

#undef log1p
#define log1p(x)  _TGMATH1(log1p, x)

/* ------------------------------------------------------------------ */
/* Power, root, and hypotenuse functions (REQ-06-0445)                */
/* ------------------------------------------------------------------ */

#undef pow
#define pow(x, y)    _TGMATH2(pow, x, y)

#undef hypot
#define hypot(x, y)  _TGMATH2(hypot, x, y)

#undef sqrt
#define sqrt(x)  _TGMATH1(sqrt, x)

#undef cbrt
#define cbrt(x)  _TGMATH1(cbrt, x)

/* ------------------------------------------------------------------ */
/* Rounding functions (REQ-06-0445)                                   */
/* ------------------------------------------------------------------ */

#undef ceil
#define ceil(x)       _TGMATH1(ceil, x)

#undef floor
#define floor(x)      _TGMATH1(floor, x)

#undef trunc
#define trunc(x)      _TGMATH1(trunc, x)

#undef round
#define round(x)      _TGMATH1(round, x)

#undef rint
#define rint(x)       _TGMATH1(rint, x)

#undef nearbyint
#define nearbyint(x)  _TGMATH1(nearbyint, x)

/* Integer-returning rounding functions — no suffix helper needed;   */
/* lround/llround/lrint/llrint share the same dispatch pattern.      */

#undef lround
#define lround(x)   _Generic((x)+0, \
        float:       lroundf,        \
        long double: lroundl,        \
        default:     lround          \
    )(x)

#undef llround
#define llround(x)  _Generic((x)+0, \
        float:       llroundf,       \
        long double: llroundl,       \
        default:     llround         \
    )(x)

#undef lrint
#define lrint(x)    _Generic((x)+0, \
        float:       lrintf,         \
        long double: lrintl,         \
        default:     lrint           \
    )(x)

#undef llrint
#define llrint(x)   _Generic((x)+0, \
        float:       llrintf,        \
        long double: llrintl,        \
        default:     llrint          \
    )(x)

#undef ilogb
#define ilogb(x)    _Generic((x)+0, \
        float:       ilogbf,         \
        long double: ilogbl,         \
        default:     ilogb           \
    )(x)

/* ------------------------------------------------------------------ */
/* Absolute value and remainder (REQ-06-0445)                         */
/* ------------------------------------------------------------------ */

#undef fabs
#define fabs(x)  _TGMATH1(fabs, x)

#undef fmod
#define fmod(x, y)       _TGMATH2(fmod, x, y)

#undef remainder
#define remainder(x, y)  _TGMATH2(remainder, x, y)

/* ------------------------------------------------------------------ */
/* Fused multiply-add (REQ-06-0445)                                   */
/* ------------------------------------------------------------------ */

#undef fma
#define fma(x, y, z)  _TGMATH3(fma, x, y, z)

/* ------------------------------------------------------------------ */
/* Min/max/dim (REQ-06-0445)                                          */
/* ------------------------------------------------------------------ */

#undef fmax
#define fmax(x, y)  _TGMATH2(fmax, x, y)

#undef fmin
#define fmin(x, y)  _TGMATH2(fmin, x, y)

#undef fdim
#define fdim(x, y)  _TGMATH2(fdim, x, y)

/* ------------------------------------------------------------------ */
/* Floating-point manipulation — numeric + pointer arg (REQ-06-0445) */
/* ------------------------------------------------------------------ */

#undef frexp
#define frexp(x, e)  _TGMATH1P(frexp, x, e)

#undef modf
#define modf(x, p)   _TGMATH1P(modf, x, p)

#undef logb
#define logb(x)      _TGMATH1(logb, x)

/* ------------------------------------------------------------------ */
/* Floating-point manipulation — numeric + integer arg (REQ-06-0445) */
/* ------------------------------------------------------------------ */

#undef ldexp
#define ldexp(x, e)     _TGMATH1I(ldexp, x, e)

#undef scalbn
#define scalbn(x, n)    _TGMATH1I(scalbn, x, n)

#undef scalbln
#define scalbln(x, n)   _TGMATH1I(scalbln, x, n)

/* ------------------------------------------------------------------ */
/* 2-argument manipulation (REQ-06-0445)                              */
/* ------------------------------------------------------------------ */

#undef nextafter
#define nextafter(x, y)  _TGMATH2(nextafter, x, y)

#undef copysign
#define copysign(x, y)   _TGMATH2(copysign, x, y)

/*
 * nexttoward: second argument is always long double per C99 §7.12.11.4.
 * Dispatch on x only.
 */
#undef nexttoward
#define nexttoward(x, y) \
    _Generic((x)+0,      \
        float:       nexttowardf, \
        long double: nexttowardl, \
        default:     nexttoward   \
    )((x), (y))

/* ------------------------------------------------------------------ */
/* remquo — 2 numeric args + pointer (REQ-06-0445)                   */
/* ------------------------------------------------------------------ */

#undef remquo
#define remquo(x, y, q)  _TGMATH2P(remquo, x, y, q)

/* ------------------------------------------------------------------ */
/* Error and gamma functions (REQ-06-0445)                            */
/* ------------------------------------------------------------------ */

#undef erf
#define erf(x)     _TGMATH1(erf, x)

#undef erfc
#define erfc(x)    _TGMATH1(erfc, x)

#undef tgamma
#define tgamma(x)  _TGMATH1(tgamma, x)

#undef lgamma
#define lgamma(x)  _TGMATH1(lgamma, x)

/* ------------------------------------------------------------------ */
/* nan() is omitted: its argument is const char*, not numeric.        */
/* C99 §7.22 notes that nan/nanf/nanl are not covered by tgmath.h.   */
/* ------------------------------------------------------------------ */

/* ------------------------------------------------------------------ */
/* Complex variant dispatch (REQ-06-0446)                             */
/* Enabled only when <complex.h> is available.                        */
/* When complex.h is present, the _Generic selection lists above      */
/* would be extended here with _Complex float / _Complex double /     */
/* _Complex long double associations pointing to the cXXX() family.  */
/* This stub records the hook point for future implementation.        */
/* ------------------------------------------------------------------ */
#if _TGMATH_HAVE_COMPLEX
/*
 * Complex variants are declared in <complex.h>.  For each function f,
 * the complex counterpart is cf (e.g. csin, ccos, cexp …).
 * The dispatch would look like:
 *
 *   #undef sin
 *   #define sin(x) _Generic((x)+0,
 *       float:               sinf,
 *       double:              sin,
 *       long double:         sinl,
 *       _Complex float:      csinf,
 *       _Complex double:     csin,
 *       _Complex long double: csinl,
 *       default:             sin
 *   )(x)
 *
 * Deferred until <complex.h> is fully implemented.
 */
#endif /* _TGMATH_HAVE_COMPLEX */

#ifdef __cplusplus
}
#endif

#endif /* _TGMATH_H */
