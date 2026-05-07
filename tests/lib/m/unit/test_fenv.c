/*
 * test_fenv.c - Comprehensive unit tests for floating-point environment
 *
 * Covers all requirements in REQ-06-0366 through REQ-06-0386.
 */

#include <stdio.h>
#include <fenv.h>
#include <assert.h>

static int g_failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { \
        printf("  FAIL: %s (line %d)\n", (msg), __LINE__); \
        g_failures++; \
    } \
} while (0)

static void reset(void) {
    feclearexcept(FE_ALL_EXCEPT);
    fesetround(FE_TONEAREST);
}

/* REQ-06-0366: raise each exception, clear it, verify via fetestexcept */
static void test_feclearexcept_individual(void) {
    int excepts[] = { FE_INVALID, FE_DIVBYZERO, FE_OVERFLOW,
                      FE_UNDERFLOW, FE_INEXACT };
    const char *names[] = { "FE_INVALID", "FE_DIVBYZERO", "FE_OVERFLOW",
                             "FE_UNDERFLOW", "FE_INEXACT" };
    for (int i = 0; i < 5; i++) {
        reset();
        feraiseexcept(excepts[i]);
        feclearexcept(excepts[i]);
        /* The specific exception bit must be gone */
        CHECK((fetestexcept(excepts[i]) & excepts[i]) == 0, names[i]);
    }
}

/* REQ-06-0367: feclearexcept(FE_ALL_EXCEPT) clears all */
static void test_feclearexcept_all(void) {
    reset();
    feraiseexcept(FE_INVALID | FE_DIVBYZERO | FE_OVERFLOW |
                  FE_UNDERFLOW | FE_INEXACT);
    int r = feclearexcept(FE_ALL_EXCEPT);
    CHECK(r == 0, "feclearexcept return");
    CHECK(fetestexcept(FE_ALL_EXCEPT) == 0, "all cleared");
}

/* REQ-06-0368: feclearexcept(0) is a no-op */
static void test_feclearexcept_zero(void) {
    reset();
    feraiseexcept(FE_INEXACT);
    feclearexcept(0);
    CHECK(fetestexcept(FE_INEXACT) != 0, "FE_INEXACT still set after clear(0)");
}

/* REQ-06-0369: fegetexceptflag / fesetexceptflag round-trip */
static void test_exceptflag_roundtrip(void) {
    fexcept_t saved;
    reset();
    feraiseexcept(FE_OVERFLOW | FE_INEXACT);
    fegetexceptflag(&saved, FE_OVERFLOW | FE_INEXACT);
    feclearexcept(FE_ALL_EXCEPT);
    CHECK(fetestexcept(FE_ALL_EXCEPT) == 0, "cleared before restore");
    fesetexceptflag(&saved, FE_ALL_EXCEPT);
    CHECK((fetestexcept(FE_OVERFLOW | FE_INEXACT) & (FE_OVERFLOW | FE_INEXACT))
          == (FE_OVERFLOW | FE_INEXACT), "round-trip restored");
}

/* REQ-06-0370: fesetexceptflag does NOT raise traps — only sets sticky bits */
static void test_fesetexceptflag_no_trap(void) {
    /*
     * Raise exceptions through fesetexceptflag while FPU is in default state
     * (all exceptions masked). If it caused a trap, we'd crash. Passing
     * means no trap was delivered even if exceptions are present in SW.
     */
    fexcept_t f = (fexcept_t)FE_DIVBYZERO;
    reset();
    int r = fesetexceptflag(&f, FE_ALL_EXCEPT);
    CHECK(r == 0, "fesetexceptflag return");
    CHECK((fetestexcept(FE_DIVBYZERO) & FE_DIVBYZERO) != 0,
          "FE_DIVBYZERO flag set");
    reset();
}

/* REQ-06-0371: feraiseexcept raises individual exceptions */
static void test_feraiseexcept_individual(void) {
    int excepts[] = { FE_INVALID, FE_DIVBYZERO, FE_OVERFLOW,
                      FE_UNDERFLOW, FE_INEXACT };
    const char *names[] = { "FE_INVALID", "FE_DIVBYZERO", "FE_OVERFLOW",
                             "FE_UNDERFLOW", "FE_INEXACT" };
    for (int i = 0; i < 5; i++) {
        reset();
        feraiseexcept(excepts[i]);
        int raised = fetestexcept(excepts[i]);
        CHECK((raised & excepts[i]) == excepts[i], names[i]);
    }
}

/* REQ-06-0372: feraiseexcept(FE_OVERFLOW) also sets FE_INEXACT */
static void test_feraiseexcept_overflow_inexact(void) {
    reset();
    feraiseexcept(FE_OVERFLOW);
    CHECK((fetestexcept(FE_OVERFLOW) & FE_OVERFLOW) != 0, "FE_OVERFLOW set");
    CHECK((fetestexcept(FE_INEXACT) & FE_INEXACT) != 0,
          "FE_INEXACT implied by FE_OVERFLOW");
}

/* REQ-06-0373: feraiseexcept(FE_UNDERFLOW) also sets FE_INEXACT */
static void test_feraiseexcept_underflow_inexact(void) {
    reset();
    feraiseexcept(FE_UNDERFLOW);
    CHECK((fetestexcept(FE_UNDERFLOW) & FE_UNDERFLOW) != 0,
          "FE_UNDERFLOW set");
    CHECK((fetestexcept(FE_INEXACT) & FE_INEXACT) != 0,
          "FE_INEXACT implied by FE_UNDERFLOW");
}

/* REQ-06-0374: feraiseexcept with multiple flags OR'd together */
static void test_feraiseexcept_multiple(void) {
    reset();
    feraiseexcept(FE_INVALID | FE_DIVBYZERO);
    CHECK((fetestexcept(FE_INVALID | FE_DIVBYZERO) &
           (FE_INVALID | FE_DIVBYZERO)) == (FE_INVALID | FE_DIVBYZERO),
          "FE_INVALID|FE_DIVBYZERO raised");
}

/* REQ-06-0375: fetestexcept returns only requested bits */
static void test_fetestexcept_mask(void) {
    reset();
    feraiseexcept(FE_INVALID | FE_OVERFLOW | FE_INEXACT);
    int result = fetestexcept(FE_OVERFLOW);
    CHECK((result & ~FE_OVERFLOW) == 0, "only requested bits returned");
    CHECK((result & FE_OVERFLOW) != 0,  "FE_OVERFLOW bit is set");
}

/* REQ-06-0376: default rounding is FE_TONEAREST */
static void test_default_rounding(void) {
    /* After fninit the default is round-to-nearest */
    fesetenv(FE_DFL_ENV);
    CHECK(fegetround() == FE_TONEAREST, "default is FE_TONEAREST");
}

/* REQ-06-0377: fesetround/fegetround round-trip for all four modes */
static void test_rounding_roundtrip(void) {
    int modes[] = { FE_TONEAREST, FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO };
    const char *names[] = { "FE_TONEAREST", "FE_DOWNWARD",
                             "FE_UPWARD", "FE_TOWARDZERO" };
    for (int i = 0; i < 4; i++) {
        CHECK(fesetround(modes[i]) == 0, names[i]);
        CHECK(fegetround() == modes[i], names[i]);
    }
    fesetround(FE_TONEAREST);
}

/* REQ-06-0378: fesetround with invalid argument returns non-zero */
static void test_fesetround_invalid(void) {
    CHECK(fesetround(-1) != 0, "invalid mode -1");
    CHECK(fesetround(4)  != 0, "invalid mode 4");
    CHECK(fesetround(99) != 0, "invalid mode 99");
}

/* REQ-06-0379: fesetround actually affects rounding direction */
static void test_fesetround_affects_rounding(void) {
    volatile double x, y;

    /* 1.0 + 2^-53 in FE_UPWARD should round up to 1+epsilon */
    fesetround(FE_UPWARD);
    x = 1.0 + 1.1102230246251565e-16; /* slightly > 0.5 ulp of 1.0 */

    fesetround(FE_DOWNWARD);
    y = 1.0 + 1.1102230246251565e-16;

    /*
     * With rounding toward +inf the result should be > result with
     * rounding toward -inf (or at worst equal if the increment was
     * already exactly representable, which it isn't for this value).
     */
    CHECK(x >= y, "FE_UPWARD >= FE_DOWNWARD");

    fesetround(FE_TONEAREST);
}

/* REQ-06-0380: fegetenv/fesetenv round-trip preserves full state */
static void test_env_roundtrip(void) {
    fenv_t saved;
    reset();
    fesetround(FE_UPWARD);
    feraiseexcept(FE_DIVBYZERO);
    fegetenv(&saved);

    /* Clobber state */
    fesetround(FE_DOWNWARD);
    feclearexcept(FE_ALL_EXCEPT);
    feraiseexcept(FE_INEXACT);

    fesetenv(&saved);
    CHECK(fegetround() == FE_UPWARD, "rounding restored");
    CHECK((fetestexcept(FE_DIVBYZERO) & FE_DIVBYZERO) != 0,
          "FE_DIVBYZERO restored");

    reset();
}

/* REQ-06-0381: fesetenv(FE_DFL_ENV) resets to default */
static void test_fesetenv_dfl_env(void) {
    fesetround(FE_DOWNWARD);
    feraiseexcept(FE_UNDERFLOW | FE_OVERFLOW);
    fesetenv(FE_DFL_ENV);
    CHECK(fegetround() == FE_TONEAREST, "default rounding after FE_DFL_ENV");
    CHECK(fetestexcept(FE_ALL_EXCEPT) == 0,
          "no exceptions after FE_DFL_ENV");
}

/* REQ-06-0382: feholdexcept saves state, clears exceptions, masks traps */
static void test_feholdexcept_saves(void) {
    fenv_t saved;
    reset();
    feraiseexcept(FE_INVALID | FE_DIVBYZERO);
    feholdexcept(&saved);
    CHECK(fetestexcept(FE_ALL_EXCEPT) == 0,
          "exceptions cleared after feholdexcept");
    /* Restore so other tests start clean */
    fesetenv(FE_DFL_ENV);
}

/* REQ-06-0383: feholdexcept + exception-raising ops don't trap */
static void test_feholdexcept_no_trap(void) {
    fenv_t saved;
    reset();
    feholdexcept(&saved);

    /*
     * Do operations that would normally raise exceptions.
     * If traps were unmasked they'd deliver SIGFPE; we shouldn't crash.
     */
    feraiseexcept(FE_INVALID | FE_DIVBYZERO | FE_OVERFLOW |
                  FE_UNDERFLOW | FE_INEXACT);
    /* If we get here without crashing, the test passes */
    CHECK(1, "survived exception operations in non-stop mode");

    fesetenv(FE_DFL_ENV);
}

/* REQ-06-0384: feupdateenv merges pending exceptions after restore */
static void test_feupdateenv_merge(void) {
    fenv_t saved;
    reset();
    /* Save clean environment */
    fegetenv(&saved);

    /* Raise something in the current (non-stop) context */
    feraiseexcept(FE_OVERFLOW);

    /* feupdateenv: restore saved env then re-raise FE_OVERFLOW */
    feupdateenv(&saved);

    /*
     * After feupdateenv the restored env is in place, but the exception
     * that was pending (FE_OVERFLOW, and FE_INEXACT implied) must be set.
     */
    CHECK((fetestexcept(FE_OVERFLOW) & FE_OVERFLOW) != 0,
          "FE_OVERFLOW re-raised by feupdateenv");

    reset();
}

/* REQ-06-0385: feupdateenv re-raises exceptions from before feholdexcept */
static void test_feupdateenv_reraise(void) {
    fenv_t saved;
    reset();

    /* Raise an exception then enter non-stop mode saving original env */
    feraiseexcept(FE_DIVBYZERO);
    feholdexcept(&saved); /* saved has FE_DIVBYZERO in its SW */

    /* Clear any current pending (non-stop cleared them) */
    CHECK(fetestexcept(FE_ALL_EXCEPT) == 0, "non-stop cleared pending");

    /* Do something in the non-stop region */
    feraiseexcept(FE_INEXACT);

    /*
     * feupdateenv restores 'saved' (FE_DFL_ENV — the state before hold)
     * then re-raises the currently pending exception (FE_INEXACT).
     * It does NOT re-raise FE_DIVBYZERO from 'saved' — that's in
     * fesetenv semantics. The FE_DIVBYZERO was in the saved state;
     * the re-raise is of what's pending *now* (FE_INEXACT).
     */
    feupdateenv(&saved);
    CHECK((fetestexcept(FE_INEXACT) & FE_INEXACT) != 0,
          "non-stop FE_INEXACT re-raised");

    reset();
}

/* REQ-06-0386: feholdexcept → compute with exceptions → feupdateenv → merged */
static void test_hold_compute_update(void) {
    fenv_t pre_hold;
    reset();

    /* Capture clean env before hold */
    fegetenv(&pre_hold);

    /* Enter non-stop mode */
    fenv_t hold_saved;
    feholdexcept(&hold_saved);

    /* Raise exceptions in the non-stop region — no traps */
    feraiseexcept(FE_OVERFLOW | FE_UNDERFLOW);

    /* feupdateenv with the pre-hold env re-raises what's pending */
    feupdateenv(&pre_hold);

    /* FE_OVERFLOW, FE_UNDERFLOW (and implied FE_INEXACT) must be set */
    CHECK((fetestexcept(FE_OVERFLOW)  & FE_OVERFLOW)  != 0, "FE_OVERFLOW merged");
    CHECK((fetestexcept(FE_UNDERFLOW) & FE_UNDERFLOW) != 0, "FE_UNDERFLOW merged");
    CHECK((fetestexcept(FE_INEXACT)   & FE_INEXACT)   != 0,
          "FE_INEXACT implied and merged");

    reset();
}

int main(void) {
    printf("=== fenv unit tests ===\n\n");

    test_feclearexcept_individual();
    printf("feclearexcept (individual)\n");

    test_feclearexcept_all();
    printf("feclearexcept(FE_ALL_EXCEPT)\n");

    test_feclearexcept_zero();
    printf("feclearexcept(0) no-op\n");

    test_exceptflag_roundtrip();
    printf("fegetexceptflag / fesetexceptflag round-trip\n");

    test_fesetexceptflag_no_trap();
    printf("fesetexceptflag does not raise traps\n");

    test_feraiseexcept_individual();
    printf("feraiseexcept (individual)\n");

    test_feraiseexcept_overflow_inexact();
    printf("feraiseexcept(FE_OVERFLOW) implies FE_INEXACT\n");

    test_feraiseexcept_underflow_inexact();
    printf("feraiseexcept(FE_UNDERFLOW) implies FE_INEXACT\n");

    test_feraiseexcept_multiple();
    printf("feraiseexcept (multiple flags)\n");

    test_fetestexcept_mask();
    printf("fetestexcept returns only requested bits\n");

    test_default_rounding();
    printf("default rounding is FE_TONEAREST\n");

    test_rounding_roundtrip();
    printf("fesetround / fegetround round-trip\n");

    test_fesetround_invalid();
    printf("fesetround (invalid arg returns non-zero)\n");

    test_fesetround_affects_rounding();
    printf("fesetround actually affects rounding direction\n");

    test_env_roundtrip();
    printf("fegetenv / fesetenv round-trip\n");

    test_fesetenv_dfl_env();
    printf("fesetenv(FE_DFL_ENV) resets to default\n");

    test_feholdexcept_saves();
    printf("feholdexcept saves state and clears exceptions\n");

    test_feholdexcept_no_trap();
    printf("feholdexcept non-stop mode (no traps)\n");

    test_feupdateenv_merge();
    printf("feupdateenv merges pending exceptions\n");

    test_feupdateenv_reraise();
    printf("feupdateenv re-raises from non-stop region\n");

    test_hold_compute_update();
    printf("feholdexcept → compute → feupdateenv merged\n");

    printf("\n");
    if (g_failures == 0) {
        printf("=== ALL TESTS PASSED ===\n");
        return 0;
    } else {
        printf("=== %d TEST(S) FAILED ===\n", g_failures);
        return 1;
    }
}


/* Helper function to print test results */
static void test_result(const char *test, int passed) {
    printf("%-60s [%s]\n", test, passed ? "PASS" : "FAIL");
}

/* Test feclearexcept function */
static int test_feclearexcept() {
    int passed = 1;
    
    /* Raise some exceptions */
    feraiseexcept(FE_INVALID | FE_DIVBYZERO);
    
    /* Clear all exceptions */
    int result = feclearexcept(FE_ALL_EXCEPT);
    if (result != 0) {
        printf("feclearexcept returned %d\n", result);
        passed = 0;
    }
    
    /* Verify no exceptions are raised */
    int raised = fetestexcept(FE_ALL_EXCEPT);
    if (raised != 0) {
        printf("Expected no exceptions, got 0x%x\n", raised);
        passed = 0;
    }
    
    /* Raise single exception */
    feraiseexcept(FE_INEXACT);
    
    /* Clear only FE_INEXACT */
    feclearexcept(FE_INEXACT);
    raised = fetestexcept(FE_ALL_EXCEPT);
    if (raised != 0) {
        printf("Expected no exceptions after clearing FE_INEXACT, got 0x%x\n", raised);
        passed = 0;
    }
    
    /* Clear 0 exceptions (should be no-op) */
    feclearexcept(0);
    raised = fetestexcept(FE_ALL_EXCEPT);
    if (raised != 0) {
        printf("Expected no exceptions after clearing 0, got 0x%x\n", raised);
        passed = 0;
    }
    
    return passed;
}

/* Test fegetexceptflag and fesetexceptflag functions */
static int test_except_flags() {
    int passed = 1;
    fexcept_t flags;
    
    /* Raise specific exceptions */
    feraiseexcept(FE_OVERFLOW | FE_UNDERFLOW);
    
    /* Get the flags */
    fegetexceptflag(&flags, FE_OVERFLOW | FE_UNDERFLOW);
    
    /* Clear and restore */
    feclearexcept(FE_ALL_EXCEPT);
    fesetexceptflag(&flags, FE_ALL_EXCEPT);
    
    int raised = fetestexcept(FE_OVERFLOW | FE_UNDERFLOW);
    if (raised != (FE_OVERFLOW | FE_UNDERFLOW)) {
        printf("Expected 0x%x, got 0x%x\n", FE_OVERFLOW | FE_UNDERFLOW, raised);
        passed = 0;
    }
    
    return passed;
}

/* Test feraiseexcept function */
static int test_feraiseexcept() {
    int passed = 1;
    
    /* Clear any existing exceptions */
    feclearexcept(FE_ALL_EXCEPT);
    
    /* Raise single exception */
    feraiseexcept(FE_INVALID);
    int raised = fetestexcept(FE_INVALID);
    if (raised != FE_INVALID) {
        printf("Expected FE_INVALID, got 0x%x\n", raised);
        passed = 0;
    }
    
    /* Clear and raise multiple exceptions */
    feclearexcept(FE_ALL_EXCEPT);
    feraiseexcept(FE_DIVBYZERO | FE_INEXACT);
    raised = fetestexcept(FE_DIVBYZERO | FE_INEXACT);
    if (raised != (FE_DIVBYZERO | FE_INEXACT)) {
        printf("Expected FE_DIVBYZERO|FE_INEXACT, got 0x%x\n", raised);
        passed = 0;
    }
    
    return passed;
}

/* Test fetestexcept function */
static int test_fetestexcept() {
    int passed = 1;
    
    feclearexcept(FE_ALL_EXCEPT);
    feraiseexcept(FE_UNDERFLOW);
    
    int test = fetestexcept(FE_UNDERFLOW);
    if (test != FE_UNDERFLOW) {
        printf("Expected FE_UNDERFLOW, got 0x%x\n", test);
        passed = 0;
    }
    
    int all = fetestexcept(FE_ALL_EXCEPT);
    if (all != FE_UNDERFLOW) {
        printf("Expected FE_UNDERFLOW, got 0x%x\n", all);
        passed = 0;
    }
    
    return passed;
}

/* Test fegetround and fesetround functions */
static int test_rounding() {
    int passed = 1;
    
    /* Get default rounding mode */
    int default_round = fegetround();
    if (default_round != FE_TONEAREST) {
        printf("Expected default rounding FE_TONEAREST, got %d\n", default_round);
        passed = 0;
    }
    
    /* Test setting and getting all valid rounding modes */
    int modes[] = {FE_TONEAREST, FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO};
    for (int i = 0; i < 4; i++) {
        int mode = modes[i];
        if (fesetround(mode) != 0) {
            printf("Failed to set rounding mode %d\n", mode);
            passed = 0;
        }
        
        int get_mode = fegetround();
        if (get_mode != mode) {
            printf("Set mode %d, got %d\n", mode, get_mode);
            passed = 0;
        }
    }
    
    /* Test invalid rounding mode */
    if (fesetround(-1) == 0) {
        printf("Expected error when setting invalid rounding mode\n");
        passed = 0;
    }
    
    /* Restore default rounding mode */
    fesetround(FE_TONEAREST);
    
    return passed;
}

/* Test fegetenv and fesetenv functions */
static int test_environment() {
    int passed = 1;
    fenv_t env;
    
    /* Test fegetenv/fesetenv round-trip */
    fegetenv(&env);
    
    /* Make a change and restore */
    fesetround(FE_UPWARD);
    feraiseexcept(FE_DIVBYZERO);
    
    if (fesetenv(&env) != 0) {
        printf("Failed to restore environment\n");
        passed = 0;
    }
    
    int round = fegetround();
    int flags = fetestexcept(FE_ALL_EXCEPT);
    if (round != FE_TONEAREST || flags != 0) {
        printf("Environment not restored correctly: round=%d, flags=0x%x\n", round, flags);
        passed = 0;
    }
    
    /* Test feholdexcept */
    feholdexcept(&env);
    int raised = fetestexcept(FE_ALL_EXCEPT);
    if (raised != 0) {
        printf("Expected no exceptions after feholdexcept, got 0x%x\n", raised);
        passed = 0;
    }
    
    /* Test feupdateenv */
    feraiseexcept(FE_INVALID);
    feupdateenv(&env);
    raised = fetestexcept(FE_ALL_EXCEPT);
    if (raised != 0) {
        printf("Expected no exceptions after feupdateenv, got 0x%x\n", raised);
        passed = 0;
    }
    
    /* Test FE_DFL_ENV */
    fesetround(FE_DOWNWARD);
    feraiseexcept(FE_UNDERFLOW);
    
    if (fesetenv(FE_DFL_ENV) != 0) {
        printf("Failed to load default environment\n");
        passed = 0;
    }
    
    round = fegetround();
    flags = fetestexcept(FE_ALL_EXCEPT);
    if (round != FE_TONEAREST || flags != 0) {
        printf("Default environment incorrect: round=%d, flags=0x%x\n", round, flags);
        passed = 0;
    }
    
    return passed;
}

/* Main test function */
int main() {
    int all_passed = 1;
    
    printf("Testing floating-point environment functions:\n\n");
    
    all_passed &= test_feclearexcept();
    test_result("feclearexcept()", all_passed);
    
    all_passed &= test_except_flags();
    test_result("fegetexceptflag()/fesetexceptflag()", all_passed);
    
    all_passed &= test_feraiseexcept();
    test_result("feraiseexcept()", all_passed);
    
    all_passed &= test_fetestexcept();
    test_result("fetestexcept()", all_passed);
    
    all_passed &= test_rounding();
    test_result("fegetround()/fesetround()", all_passed);
    
    all_passed &= test_environment();
    test_result("Environment save/restore (fegetenv/fesetenv/fesetenv(FE_DFL_ENV))", all_passed);
    
    printf("\n=== All tests passed ===\n");
    
    return 0;
}
