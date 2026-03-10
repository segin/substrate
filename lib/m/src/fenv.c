/*
 * fenv.c - Floating-point environment functions for i386
 *
 * Implements the C99/POSIX floating-point environment interface
 * using x87 FPU instructions. SSE support is stubbed.
 *
 * All functions are __attribute__((noinline)) to prevent
 * compiler from reordering FPU state operations.
 */

#include <fenv.h>

/* Helper to extract rounding mode from control word */
static int __get_round_mode(unsigned short cw) {
    return (cw >> 10) & 0x03;
}

/* Helper to set rounding mode in control word */
static unsigned short __set_round_mode(unsigned short cw, int mode) {
    return (cw & ~(0x03 << 10)) | ((mode & 0x03) << 10);
}

/* Helper to check if rounding mode is valid */
static int __is_valid_round_mode(int mode) {
    switch (mode) {
        case FE_TONEAREST:
        case FE_DOWNWARD:
        case FE_UPWARD:
        case FE_TOWARDZERO:
            return 1;
        default:
            return 0;
    }
}

/* Clear specified exceptions */
int feclearexcept(int excepts) {
    fenv_t env;
    
    /* Save current environment */
    __asm__ __volatile__("fnstenv %0" : "=m"(env));
    
    /* Clear specified exception bits in status word */
    env.__status_word &= ~(excepts & FE_ALL_EXCEPT);
    
    /* Load modified environment */
    __asm__ __volatile__("fldenv %0" : : "m"(env));
    
    return 0;
}

/* Get exception flags */
int fegetexceptflag(fexcept_t *flagp, int excepts) {
    unsigned short sw;
    
    /* Get status word */
    __asm__ __volatile__("fnstsw %0" : "=m"(sw));
    
    /* Mask and store requested exception bits */
    *flagp = sw & (excepts & FE_ALL_EXCEPT);
    
    return 0;
}

/* Raise specified exceptions */
int feraiseexcept(int excepts) {
    fenv_t env;
    
    /* Save current environment */
    fegetenv(&env);
    
    /* Unmask exceptions so they can be raised */
    fenv_t temp_env = env;
    temp_env.__control_word &= ~(excepts & FE_ALL_EXCEPT);
    fesetenv(&temp_env);
    
    /* Raise exceptions by performing operations that trigger them */
    if (excepts & FE_INVALID) {
        volatile float f = 0.0f / 0.0f;
        (void)f;
    }
    if (excepts & FE_DIVBYZERO) {
        volatile float f = 1.0f / 0.0f;
        (void)f;
    }
    if (excepts & FE_OVERFLOW) {
        volatile float f = __builtin_huge_valf() * 2.0f;
        (void)f;
    }
    if (excepts & FE_UNDERFLOW) {
        volatile float f = 1e-38f / 2.0f;
        (void)f;
    }
    if (excepts & FE_INEXACT) {
        volatile float f = 1.0f / 3.0f;
        (void)f;
    }
    
    /* Restore original environment */
    fesetenv(&env);
    
    return 0;
}

/* Set exception flags */
int fesetexceptflag(const fexcept_t *flagp, int excepts) {
    fenv_t env;
    
    /* Save current environment */
    __asm__ __volatile__("fnstenv %0" : "=m"(env));
    
    /* Replace specified exception bits in status word */
    env.__status_word = (env.__status_word & ~(excepts & FE_ALL_EXCEPT)) |
                       (*flagp & (excepts & FE_ALL_EXCEPT));
    
    /* Load modified environment */
    __asm__ __volatile__("fldenv %0" : : "m"(env));
    
    return 0;
}

/* Test if any of the specified exceptions are raised */
int fetestexcept(int excepts) {
    unsigned short sw;
    
    /* Get status word */
    __asm__ __volatile__("fnstsw %0" : "=m"(sw));
    
    return sw & (excepts & FE_ALL_EXCEPT);
}

/* Get current rounding mode */
int fegetround(void) {
    unsigned short cw;
    
    /* Get control word */
    __asm__ __volatile__("fnstcw %0" : "=m"(cw));
    
    return __get_round_mode(cw);
}

/* Set rounding mode */
int fesetround(int rdir) {
    unsigned short cw;
    
    /* Validate rounding mode */
    if (!__is_valid_round_mode(rdir)) {
        return -1;
    }
    
    /* Get current control word */
    __asm__ __volatile__("fnstcw %0" : "=m"(cw));
    
    /* Update rounding mode */
    cw = __set_round_mode(cw, rdir);
    
    /* Load new control word */
    __asm__ __volatile__("fldcw %0" : : "m"(cw));
    
    return 0;
}

/* Get current environment */
int fegetenv(fenv_t *envp) {
    /* Save x87 environment */
    __asm__ __volatile__("fnstenv %0" : "=m"(*envp));
    
    /* Save MXCSR (stub for now) */
    envp->__mxcsr = 0;
    
    /* fnstenv masks all exceptions, so restore control word */
    __asm__ __volatile__("fldcw %0" : : "m"(envp->__control_word));
    
    return 0;
}

/* Set environment */
int fesetenv(const fenv_t *envp) {
    if (envp == FE_DFL_ENV) {
        /* Load default environment */
        __asm__ __volatile__("fninit");
        return 0;
    }
    
    /* Load x87 environment */
    __asm__ __volatile__("fldenv %0" : : "m"(*envp));
    
    /* Load MXCSR (stub for now) */
    (void)envp->__mxcsr;
    
    return 0;
}

/* Hold exceptions (enter non-stop mode) */
int feholdexcept(fenv_t *envp) {
    /* Save current environment */
    __asm__ __volatile__("fnstenv %0" : "=m"(*envp));
    
    /* Modify environment: clear exceptions, mask all traps */
    envp->__status_word &= ~FE_ALL_EXCEPT;
    envp->__control_word |= FE_ALL_EXCEPT;
    
    /* Load modified environment */
    __asm__ __volatile__("fldenv %0" : : "m"(*envp));
    
    return 0;
}

/* Update environment and re-raise exceptions */
int feupdateenv(const fenv_t *envp) {
    int exceptions;
    
    /* Get currently raised exceptions */
    exceptions = fetestexcept(FE_ALL_EXCEPT);
    
    /* Load new environment */
    fesetenv(envp);
    
    /* Re-raise exceptions */
    if (exceptions) {
        feraiseexcept(exceptions);
    }
    
    return 0;
}
