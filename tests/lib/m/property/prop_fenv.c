/*
 * prop_fenv.c - Property-based tests for floating-point environment functions
 *
 * Covers requirements REQ-06-0388 through REQ-06-0397.
 *
 * Each property is exercised over its full domain (all valid inputs) or
 * over a representative set of interesting values.
 */

#include <stdio.h>
#include <fenv.h>

static int g_failures = 0;

#define PROP_CHECK(cond, name) do { \
    if (!(cond)) { \
        printf("  FAIL property: %s (line %d)\n", (name), __LINE__); \
        g_failures++; \
    } \
} while (0)

static void reset(void) {
    feclearexcept(FE_ALL_EXCEPT);
    fesetround(FE_TONEAREST);
}

/*
 * REQ-06-0388: fesetround(m); fegetround() == m  for all valid m
 */
static void prop_setround_getround(void) {
    int modes[] = { FE_TONEAREST, FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO };
    const char *names[] = { "FE_TONEAREST", "FE_DOWNWARD",
                             "FE_UPWARD", "FE_TOWARDZERO" };
    for (int i = 0; i < 4; i++) {
        fesetround(modes[i]);
        PROP_CHECK(fegetround() == modes[i], names[i]);
    }
    fesetround(FE_TONEAREST);
}

/*
 * REQ-06-0389: fesetround(invalid) returns non-zero and does not change mode
 */
static void prop_setround_invalid(void) {
    int invalid[] = { -1, 4, 5, 127, -100 };
    for (int i = 0; i < 5; i++) {
        fesetround(FE_TONEAREST);
        int before = fegetround();
        int r = fesetround(invalid[i]);
        int after = fegetround();
        PROP_CHECK(r != 0,      "invalid arg returns non-zero");
        PROP_CHECK(after == before, "mode unchanged after invalid fesetround");
    }
}

/*
 * REQ-06-0390: feclearexcept(e); fetestexcept(e) == 0  for any valid e
 */
static void prop_clear_test_zero(void) {
    /* All single-bit and multi-bit subsets of FE_ALL_EXCEPT */
    int bases[] = { FE_INVALID, FE_DIVBYZERO, FE_OVERFLOW,
                    FE_UNDERFLOW, FE_INEXACT };
    for (int i = 0; i < 5; i++) {
        for (int j = i; j < 5; j++) {
            int e = bases[i] | bases[j];
            feraiseexcept(FE_ALL_EXCEPT); /* set all first */
            feclearexcept(e);
            PROP_CHECK((fetestexcept(e) & e) == 0,
                       "feclearexcept then fetestexcept == 0");
        }
    }
    reset();
}

/*
 * REQ-06-0391: feraiseexcept(e); (fetestexcept(e) & e) == e  for any valid e
 */
static void prop_raise_test(void) {
    int bases[] = { FE_INVALID, FE_DIVBYZERO, FE_OVERFLOW,
                    FE_UNDERFLOW, FE_INEXACT };
    for (int i = 0; i < 5; i++) {
        for (int j = i; j < 5; j++) {
            int e = bases[i] | bases[j];
            reset();
            feraiseexcept(e);
            PROP_CHECK((fetestexcept(e) & e) == e,
                       "feraiseexcept then fetestexcept has all bits");
        }
    }
    reset();
}

/*
 * REQ-06-0392: fegetexceptflag / feclearexcept / fesetexceptflag round-trip
 */
static void prop_exceptflag_roundtrip(void) {
    int bases[] = { FE_INVALID, FE_DIVBYZERO, FE_OVERFLOW,
                    FE_UNDERFLOW, FE_INEXACT };
    for (int i = 0; i < 5; i++) {
        for (int j = i; j < 5; j++) {
            int e = bases[i] | bases[j];
            reset();
            feraiseexcept(e);
            fexcept_t f;
            fegetexceptflag(&f, e);
            feclearexcept(FE_ALL_EXCEPT);
            fesetexceptflag(&f, e);
            int result = fetestexcept(e);
            PROP_CHECK((result & e) == e, "round-trip flag preserved");
        }
    }
    reset();
}

/*
 * REQ-06-0393: fegetenv / (modify) / fesetenv restores original state
 */
static void prop_env_roundtrip(void) {
    int modes[] = { FE_TONEAREST, FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO };
    int excepts[] = { 0, FE_INEXACT, FE_OVERFLOW | FE_INEXACT,
                      FE_INVALID | FE_DIVBYZERO };

    for (int m = 0; m < 4; m++) {
        for (int e = 0; e < 4; e++) {
            reset();
            fesetround(modes[m]);
            feraiseexcept(excepts[e]);

            fenv_t saved;
            fegetenv(&saved);

            /* Clobber state */
            fesetround(modes[(m + 1) % 4]);
            feclearexcept(FE_ALL_EXCEPT);
            feraiseexcept(FE_UNDERFLOW);

            fesetenv(&saved);

            PROP_CHECK(fegetround() == modes[m], "rounding mode restored");
            PROP_CHECK((fetestexcept(excepts[e]) & excepts[e]) == excepts[e],
                       "exceptions restored");
        }
    }
    reset();
}

/*
 * REQ-06-0394: fesetenv(FE_DFL_ENV) → FE_TONEAREST and no exceptions
 */
static void prop_dfl_env(void) {
    int clobber_modes[] = { FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO };
    int clobber_exc[]   = { FE_INVALID, FE_OVERFLOW, FE_INEXACT };

    for (int i = 0; i < 3; i++) {
        fesetround(clobber_modes[i]);
        feraiseexcept(clobber_exc[i]);
        fesetenv(FE_DFL_ENV);
        PROP_CHECK(fegetround() == FE_TONEAREST, "FE_DFL_ENV → FE_TONEAREST");
        PROP_CHECK(fetestexcept(FE_ALL_EXCEPT) == 0,
                   "FE_DFL_ENV → no exceptions");
    }
}

/*
 * REQ-06-0395: feholdexcept → fetestexcept(FE_ALL_EXCEPT) == 0 and non-stop
 */
static void prop_feholdexcept_nonstop(void) {
    for (int trial = 0; trial < 4; trial++) {
        reset();
        feraiseexcept(FE_INVALID | FE_DIVBYZERO | FE_OVERFLOW);

        fenv_t hold;
        feholdexcept(&hold);

        PROP_CHECK(fetestexcept(FE_ALL_EXCEPT) == 0,
                   "feholdexcept cleared pending exceptions");

        /* Verify CW has all exception bits masked (bits 0-5 set) */
        fenv_t cur;
        fegetenv(&cur);
        PROP_CHECK((cur.__control_word & FE_ALL_EXCEPT) == FE_ALL_EXCEPT,
                   "feholdexcept masked all exception traps in CW");

        fesetenv(FE_DFL_ENV);
    }
}

/*
 * REQ-06-0396: feupdateenv preserves exceptions raised during non-stop region
 */
static void prop_feupdateenv_preserves(void) {
    int exc_raised[] = { FE_INEXACT, FE_OVERFLOW | FE_INEXACT,
                         FE_DIVBYZERO, FE_INVALID | FE_INEXACT };
    for (int i = 0; i < 4; i++) {
        reset();
        fenv_t clean;
        fegetenv(&clean);

        fenv_t hold;
        feholdexcept(&hold);

        feraiseexcept(exc_raised[i]);
        feupdateenv(&clean);

        /* All exceptions raised in the non-stop region must survive */
        /* Note: FE_OVERFLOW and FE_UNDERFLOW imply FE_INEXACT */
        int expected = exc_raised[i];
        if (expected & (FE_OVERFLOW | FE_UNDERFLOW))
            expected |= FE_INEXACT;
        PROP_CHECK((fetestexcept(expected) & expected) == expected,
                   "non-stop exceptions preserved by feupdateenv");
        reset();
    }
}

/*
 * REQ-06-0397: exception flags are sticky — feraiseexcept(e) followed by
 * unrelated FP ops retains e
 */
static void prop_sticky_flags(void) {
    int excepts[] = { FE_INVALID, FE_DIVBYZERO, FE_OVERFLOW,
                      FE_UNDERFLOW, FE_INEXACT };

    for (int i = 0; i < 5; i++) {
        reset();
        feraiseexcept(excepts[i]);

        /* Perform an unrelated FP operation that should not clear sticky bits */
        volatile double x = 1.0 + 1.0; /* exact, raises no exceptions */
        (void)x;

        PROP_CHECK((fetestexcept(excepts[i]) & excepts[i]) != 0,
                   "exception flag is sticky");
        reset();
    }
}

int main(void) {
    printf("=== fenv property tests ===\n\n");

    prop_setround_getround();
    printf("prop: fesetround / fegetround identity\n");

    prop_setround_invalid();
    printf("prop: fesetround(invalid) non-zero and no state change\n");

    prop_clear_test_zero();
    printf("prop: feclearexcept then fetestexcept == 0\n");

    prop_raise_test();
    printf("prop: feraiseexcept then fetestexcept has all bits\n");

    prop_exceptflag_roundtrip();
    printf("prop: fegetexceptflag / fesetexceptflag round-trip\n");

    prop_env_roundtrip();
    printf("prop: fegetenv / fesetenv round-trip\n");

    prop_dfl_env();
    printf("prop: fesetenv(FE_DFL_ENV) → default state\n");

    prop_feholdexcept_nonstop();
    printf("prop: feholdexcept enters non-stop mode\n");

    prop_feupdateenv_preserves();
    printf("prop: feupdateenv preserves non-stop exceptions\n");

    prop_sticky_flags();
    printf("prop: exception flags are sticky\n");

    printf("\n");
    if (g_failures == 0) {
        printf("=== ALL PROPERTY TESTS PASSED ===\n");
        return 0;
    } else {
        printf("=== %d PROPERTY TEST(S) FAILED ===\n", g_failures);
        return 1;
    }
}
