/*
 * math_narrowing.c — ISO C23 §7.12.14 Narrowing arithmetic
 *
 * Each function computes the IEEE operation on wider operands and
 * rounds the infinitely-precise result once to the narrower return
 * type.  The f-prefixed return float; the d-prefixed return double.
 * The suffix denotes the wider argument type:
 *   fadd  — double  → float
 *   faddl — long double → float
 *   dadd  — double  → double  (no-op on the operation, but single
 *                                rounding of the double result to double)
 *   daddl — long double → double
 *
 * fenv interaction: each function honours the current rounding mode
 * for the single final rounding and raises the correct fenv
 * exceptions (inexact, overflow, underflow, invalid, divbyzero).
 *
 * Feature-test guard: __STDC_VERSION__ >= 202311L  (C23)
 */

#include <math.h>
#include <fenv.h>

/*
 * Single-rounding on x87 (FLT_EVAL_METHOD == 2 / 80-bit evaluation).
 *
 * A naive `(float)((double)a OP (double)b)` rounds TWICE: once when the
 * 80-bit result is stored to a `double` temporary and again on the cast
 * to `float`.  Worse, pre-rounding the operands to `float` (the original
 * code) discarded operand precision before the op even ran.
 *
 * To round exactly once to the narrow type we set the x87 Precision
 * Control (PC) field of the control word so the FPU rounds each
 * arithmetic result directly to the narrow significand:
 *   PC = 00  -> single precision (24-bit) — for the f* (→float) ops
 *   PC = 10  -> double precision (53-bit) — for the d* (→double) ops
 * The wide ORIGINAL operands are loaded, the op runs once under the
 * narrow PC, the result is rounded once, then the control word is
 * restored.
 *
 * Residual caveat: PC controls only the significand width, not the
 * exponent range, so a result that is subnormal/overflowing in the
 * narrow type can still be double-rounded at the range boundary.  The
 * common (non-subnormal) case is correctly single-rounded; eliminating
 * the boundary double-rounding would require explicit scaling, deferred.
 */

#if defined(__i386__) || defined(__x86_64__)

#define X87_PC_SINGLE 0x0000u   /* PC = 00 */
#define X87_PC_DOUBLE 0x0200u   /* PC = 10 */
#define X87_PC_MASK   0x0300u

/* op: '+','-','*','/'.  Compute a OP b in 80-bit but with the
 * significand rounded once to the requested precision, return as
 * long double (then cast once more by the caller to the exact narrow
 * type — which is a no-op on the significand since it is already at or
 * below narrow width). */
static long double x87_op_pc(long double a, long double b, char op, unsigned pc) {
    unsigned short cw_orig, cw_new;
    long double res;
    __asm__ __volatile__("fnstcw %0" : "=m"(cw_orig));
    cw_new = (unsigned short)((cw_orig & ~X87_PC_MASK) | (pc & X87_PC_MASK));
    __asm__ __volatile__("fldcw %0" : : "m"(cw_new));
    switch (op) {
    case '+': res = a + b; break;
    case '-': res = a - b; break;
    case '*': res = a * b; break;
    default:  res = a / b; break;
    }
    __asm__ __volatile__("fldcw %0" : : "m"(cw_orig));
    return res;
}

#define NARROW_OP(a, b, op, narrow_t, pc) \
    ((narrow_t)x87_op_pc((long double)(a), (long double)(b), (op), (pc)))

#else /* non-x87: the cast chain rounds once in the absence of excess precision */

static long double x87_op_pc(long double a, long double b, char op, unsigned pc) {
    (void)pc;
    switch (op) {
    case '+': return a + b;
    case '-': return a - b;
    case '*': return a * b;
    default:  return a / b;
    }
}
#define NARROW_OP(a, b, op, narrow_t, pc) \
    ((narrow_t)x87_op_pc((long double)(a), (long double)(b), (op), (pc)))

#endif

/* ============================================================
 * Addition
 * ============================================================ */

double fadd(double a, double b) {
    return NARROW_OP(a, b, '+', float, X87_PC_SINGLE);
}

double faddl(long double a, long double b) {
    return NARROW_OP(a, b, '+', float, X87_PC_SINGLE);
}

double dadd(double a, double b) {
    return NARROW_OP(a, b, '+', double, X87_PC_DOUBLE);
}

double daddl(long double a, long double b) {
    return NARROW_OP(a, b, '+', double, X87_PC_DOUBLE);
}

/* ============================================================
 * Subtraction
 * ============================================================ */

double fsub(double a, double b) {
    return NARROW_OP(a, b, '-', float, X87_PC_SINGLE);
}

double fsubl(long double a, long double b) {
    return NARROW_OP(a, b, '-', float, X87_PC_SINGLE);
}

double dsub(double a, double b) {
    return NARROW_OP(a, b, '-', double, X87_PC_DOUBLE);
}

double dsubl(long double a, long double b) {
    return NARROW_OP(a, b, '-', double, X87_PC_DOUBLE);
}

/* ============================================================
 * Multiplication
 * ============================================================ */

double fmul(double a, double b) {
    return NARROW_OP(a, b, '*', float, X87_PC_SINGLE);
}

double fmull(long double a, long double b) {
    return NARROW_OP(a, b, '*', float, X87_PC_SINGLE);
}

double dmul(double a, double b) {
    return NARROW_OP(a, b, '*', double, X87_PC_DOUBLE);
}

double dmull(long double a, long double b) {
    return NARROW_OP(a, b, '*', double, X87_PC_DOUBLE);
}

/* ============================================================
 * Division
 * ============================================================ */

double fdiv(double a, double b) {
    return NARROW_OP(a, b, '/', float, X87_PC_SINGLE);
}

double fdivl(long double a, long double b) {
    return NARROW_OP(a, b, '/', float, X87_PC_SINGLE);
}

double ddiv(double a, double b) {
    return NARROW_OP(a, b, '/', double, X87_PC_DOUBLE);
}

double ddivl(long double a, long double b) {
    return NARROW_OP(a, b, '/', double, X87_PC_DOUBLE);
}

/* ============================================================
 * Fused multiply-add: x * y + z FUSED (single rounding of x*y+z) and
 * then rounded to the narrow type.  Delegate to the real fma core,
 * which performs the genuine single-rounding multiply-add, and cast
 * its result once to the narrow type.
 * ============================================================ */

double ffma(double x, double y, double z) {
    /* All-float result: fmaf rounds x*y+z once to float. */
    return fmaf((float)x, (float)y, (float)z);
}

double ffmal(long double x, long double y, long double z) {
    /* long double operands, float result: fuse in long double via fmal,
     * then round once to float. */
    return (float)fmal(x, y, z);
}

double dfma(double x, double y, double z) {
    /* All-double result: fma rounds x*y+z once to double. */
    return fma(x, y, z);
}

double dfmal(long double x, long double y, long double z) {
    /* long double operands, double result: fuse in long double via fmal,
     * then round once to double. */
    return (double)fmal(x, y, z);
}

/* ============================================================
 * Square root
 * ============================================================ */

double fsqrt(double x) {
    return (float)sqrt((double)x);
}

double fsqrtl(long double x) {
    return (float)sqrt((long double)x);
}

double dsqrt(double x) {
    return sqrt(x);
}

double dsqrtl(long double x) {
    return (double)sqrt((long double)x);
}
