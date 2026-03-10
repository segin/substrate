/*
 * test_fenv_simple.c - Simple test to verify fenv functions are accessible
 */

#include <stdio.h>
#include <fenv.h>
#include <math.h>

int main() {
    printf("Testing fenv.h functions\n");
    
    /* Test fegetround/fesetround */
    int round = fegetround();
    printf("Default rounding mode: %d\n", round);
    
    /* Test fesetround */
    if (fesetround(FE_DOWNWARD) == 0) {
        printf("Set rounding to FE_DOWNWARD: %d\n", fegetround());
    } else {
        printf("Failed to set rounding mode\n");
        return 1;
    }
    
    /* Restore default */
    fesetround(FE_TONEAREST);
    
    /* Test feclearexcept and fetestexcept */
    int excepts = fetestexcept(FE_ALL_EXCEPT);
    printf("Current exceptions: 0x%x\n", excepts);
    
    /* Test feraiseexcept */
    feraiseexcept(FE_INEXACT);
    excepts = fetestexcept(FE_ALL_EXCEPT);
    printf("Exceptions after raising FE_INEXACT: 0x%x\n", excepts);
    
    /* Test feclearexcept */
    feclearexcept(FE_INEXACT);
    excepts = fetestexcept(FE_ALL_EXCEPT);
    printf("Exceptions after clearing FE_INEXACT: 0x%x\n", excepts);
    
    /* Test fegetenv */
    fenv_t env;
    fegetenv(&env);
    printf("Environment saved successfully\n");
    
    printf("\nAll tests passed\n");
    return 0;
}
