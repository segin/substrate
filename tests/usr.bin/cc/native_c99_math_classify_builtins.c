#include <stdio.h>

static int fail(int line) {
    fprintf(stderr, "fail:%d\n", line);
    return line;
}

int main(void) {
    volatile double pos_inf = __builtin_inf();
    volatile double neg_inf = -__builtin_inf();
    volatile double nanv = __builtin_nan("");
    volatile double pos = 1.5;
    volatile double neg_zero = -0.0;
    volatile float float_nan = __builtin_nanf("");
    volatile float float_neg_zero = -0.0f;
    volatile long double ld_neg = -1.0L;
    volatile long double ld_neg_zero = -0.0L;

    if (!__builtin_isnan(nanv) || __builtin_isnan(pos)) {
        return fail(__LINE__);
    }
    if (__builtin_isinf_sign(pos_inf) != 1 || __builtin_isinf_sign(neg_inf) != -1 || __builtin_isinf_sign(pos) != 0) {
        return fail(__LINE__);
    }
    if (!__builtin_isinf(pos_inf) || !__builtin_isinf(neg_inf) || __builtin_isinf(nanv) || __builtin_isinf(pos)) {
        return fail(__LINE__);
    }
    if (!__builtin_isfinite(pos) || __builtin_isfinite(pos_inf) || __builtin_isfinite(neg_inf) ||
        __builtin_isfinite(nanv)) {
        return fail(__LINE__);
    }
    if (!__builtin_signbit(-1.0) || __builtin_signbit(1.0) || !__builtin_signbit(neg_zero)) {
        return fail(__LINE__);
    }
    if (!__builtin_isnanf(float_nan) || !__builtin_signbitf(float_neg_zero)) {
        return fail(__LINE__);
    }
    if (!__builtin_signbitl(ld_neg) || !__builtin_signbitl(ld_neg_zero) || __builtin_signbitl(1.0L)) {
        return fail(__LINE__);
    }
    return 0;
}
