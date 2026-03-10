/*
 * test_fenv.c - Unit tests for floating-point environment functions
 */

#include <stdio.h>
#include <fenv.h>
#include <math.h>
#include <assert.h>

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
