/*
 * fenv.c - Floating-point environment functions for i386
 *
 * Implements the C99/POSIX floating-point environment interface
 * using x87 FPU instructions and optional SSE (MXCSR) support.
 *
 * All functions are __attribute__((noinline)) to prevent
 * compiler from reordering FPU state operations.
 *
 * MXCSR register layout (SSE):
 *   Bits  0-5:  Exception flags (same bit positions as x87 SW bits 0-5)
 *   Bit   6:    DAZ (Denormals Are Zero)
 *   Bits  7-12: Exception masks (1 = masked/quiet)
 *   Bits 13-14: Rounding mode (same encoding as x87 CW bits 10-11)
 *   Bit  15:    Flush-to-zero
 *
 * x87 MXCSR rounding bits vs x87 CW rounding bits:
 *   x87 CW bits 10-11: RC field
 *   MXCSR bits 13-14:  RC field (same encoding)
 *
 * Note: On i386 with -mno-sse the __SSE__ macro is not defined and
 * MXCSR paths are compiled out entirely.
 */

#include <fenv.h>

/* MXCSR bit positions */
#define MXCSR_EXCEPT_MASK    0x003F   /* bits 0-5: exception flags */
#define MXCSR_EMASK_SHIFT    7        /* exception mask bits start at 7 */
#define MXCSR_EMASK_MASK     0x1F80   /* bits 7-12: exception masks */
#define MXCSR_RC_SHIFT       13       /* rounding control bits 13-14 */
#define MXCSR_RC_MASK        0x6000   /* bits 13-14: rounding control */

/* x87 FPU status word exception bits are in bits 0-5, same as FE_* macros */
/* x87 FPU control word rounding mode is in bits 10-11 */
#define FPU_CW_RC_SHIFT      10
#define FPU_CW_RC_MASK       0x0C00

/* Helper to extract rounding mode from x87 control word */
static int __get_round_mode(unsigned short cw) {
    return (cw >> FPU_CW_RC_SHIFT) & 0x03;
}

/* Helper to set rounding mode in x87 control word */
static unsigned short __set_round_mode(unsigned short cw, int mode) {
    return (unsigned short)((cw & ~FPU_CW_RC_MASK) | ((mode & 0x03) << FPU_CW_RC_SHIFT));
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

#ifdef __SSE__
static unsigned int __get_mxcsr(void) {
    unsigned int mxcsr;
    __asm__ __volatile__("stmxcsr %0" : "=m"(mxcsr));
    return mxcsr;
}

static void __set_mxcsr(unsigned int mxcsr) {
    __asm__ __volatile__("ldmxcsr %0" : : "m"(mxcsr));
}
#endif /* __SSE__ */

/* Clear specified exceptions */
__attribute__((noinline))
int feclearexcept(int excepts) {
    fenv_t env;
    int mask = excepts & FE_ALL_EXCEPT;

    /* Save full x87 environment, modify status word, reload */
    __asm__ __volatile__("fnstenv %0" : "=m"(env));
    env.__status_word = (unsigned short)(env.__status_word & ~mask);
    __asm__ __volatile__("fldenv %0" : : "m"(env));

#ifdef __SSE__
    unsigned int mxcsr = __get_mxcsr();
    mxcsr &= ~(unsigned int)mask;
    __set_mxcsr(mxcsr);
#endif

    return 0;
}

/* Get exception flags */
__attribute__((noinline))
int fegetexceptflag(fexcept_t *flagp, int excepts) {
    unsigned short sw;
    int mask = excepts & FE_ALL_EXCEPT;

    __asm__ __volatile__("fnstsw %0" : "=m"(sw));
    *flagp = (fexcept_t)(sw & mask);

#ifdef __SSE__
    *flagp = (fexcept_t)(*flagp | (__get_mxcsr() & (unsigned int)mask));
#endif

    return 0;
}

/*
 * Raise specified exceptions.
 *
 * Uses the env-manipulation method (save SW, set bits, reload, fwait) to
 * reliably raise exception flags.  Per C99 §7.6.2.3, raising FE_OVERFLOW
 * or FE_UNDERFLOW must also set FE_INEXACT.
 */
__attribute__((noinline))
int feraiseexcept(int excepts) {
    fenv_t env;
    int mask = excepts & FE_ALL_EXCEPT;

    /* FE_OVERFLOW and FE_UNDERFLOW imply FE_INEXACT (C99 7.6.2.3) */
    if (mask & (FE_OVERFLOW | FE_UNDERFLOW))
        mask |= FE_INEXACT;

    /* Save current x87 environment */
    __asm__ __volatile__("fnstenv %0" : "=m"(env));

    /* Set exception flags in status word */
    env.__status_word = (unsigned short)(env.__status_word | mask);

    /* Reload — this does NOT raise traps by itself */
    __asm__ __volatile__("fldenv %0" : : "m"(env));

    /* fwait will trigger a pending unmasked exception trap if applicable */
    __asm__ __volatile__("fwait");

#ifdef __SSE__
    unsigned int mxcsr = __get_mxcsr();
    mxcsr |= (unsigned int)mask;
    __set_mxcsr(mxcsr);
#endif

    return 0;
}

/* Set exception flags without raising them */
__attribute__((noinline))
int fesetexceptflag(const fexcept_t *flagp, int excepts) {
    fenv_t env;
    int mask = excepts & FE_ALL_EXCEPT;

    /* Save current environment */
    __asm__ __volatile__("fnstenv %0" : "=m"(env));

    /* Replace specified exception bits in status word — must NOT raise traps */
    env.__status_word = (unsigned short)((env.__status_word & ~mask) |
                                         (*flagp & mask));

    /* Reload — does not trigger traps */
    __asm__ __volatile__("fldenv %0" : : "m"(env));

#ifdef __SSE__
    unsigned int mxcsr = __get_mxcsr();
    mxcsr = (mxcsr & ~(unsigned int)mask) | ((unsigned int)*flagp & (unsigned int)mask);
    __set_mxcsr(mxcsr);
#endif

    return 0;
}

/* Test if any of the specified exceptions are raised */
__attribute__((noinline))
int fetestexcept(int excepts) {
    unsigned short sw;
    int mask = excepts & FE_ALL_EXCEPT;

    __asm__ __volatile__("fnstsw %0" : "=m"(sw));
    int result = sw & mask;

#ifdef __SSE__
    result |= (int)(__get_mxcsr() & (unsigned int)mask);
#endif

    return result;
}

/* Get current rounding mode */
__attribute__((noinline))
int fegetround(void) {
    unsigned short cw;
    __asm__ __volatile__("fnstcw %0" : "=m"(cw));
    return __get_round_mode(cw);
}

/* Set rounding mode */
__attribute__((noinline))
int fesetround(int rdir) {
    unsigned short cw;

    if (!__is_valid_round_mode(rdir))
        return -1;

    __asm__ __volatile__("fnstcw %0" : "=m"(cw));
    cw = __set_round_mode(cw, rdir);
    __asm__ __volatile__("fldcw %0" : : "m"(cw));

#ifdef __SSE__
    unsigned int mxcsr = __get_mxcsr();
    mxcsr = (mxcsr & ~(unsigned int)MXCSR_RC_MASK) |
            ((unsigned int)rdir << MXCSR_RC_SHIFT);
    __set_mxcsr(mxcsr);
#endif

    return 0;
}

/* Get current environment */
__attribute__((noinline))
int fegetenv(fenv_t *envp) {
    /* fnstenv masks all FPU exceptions as a side effect; save first */
    __asm__ __volatile__("fnstenv %0" : "=m"(*envp));

    /* Undo the fnstenv masking side effect by reloading the original CW */
    __asm__ __volatile__("fldcw %0" : : "m"(envp->__control_word));

#ifdef __SSE__
    envp->__mxcsr = __get_mxcsr();
#else
    envp->__mxcsr = 0;
#endif

    return 0;
}

/* Set environment — must NOT raise exceptions */
__attribute__((noinline))
int fesetenv(const fenv_t *envp) {
    if (envp == FE_DFL_ENV) {
        /* fninit resets x87 to default state without raising exceptions */
        __asm__ __volatile__("fninit");
#ifdef __SSE__
        /* Reset MXCSR to default: all exceptions masked, round-to-nearest */
        unsigned int default_mxcsr = 0x1F80; /* bits 7-12 set = all masked */
        __set_mxcsr(default_mxcsr);
#endif
        return 0;
    }

    /* Load x87 environment — does not cause exception traps */
    __asm__ __volatile__("fldenv %0" : : "m"(*envp));

#ifdef __SSE__
    __set_mxcsr(envp->__mxcsr);
#endif

    return 0;
}

/*
 * Hold exceptions (enter non-stop mode).
 *
 * Saves the current environment to *envp (original state, unmodified),
 * then installs a non-stop environment with:
 *   - all exception flags cleared
 *   - all exception traps masked
 */
__attribute__((noinline))
int feholdexcept(fenv_t *envp) {
    fenv_t tmp;

    /*
     * fnstenv stores the original environment into *envp and then masks
     * all FPU exception traps in the FPU's CW (side effect).
     * *envp now contains the original state (original CW, SW, etc.).
     */
    __asm__ __volatile__("fnstenv %0" : "=m"(*envp));

#ifdef __SSE__
    envp->__mxcsr = __get_mxcsr();
#else
    envp->__mxcsr = 0;
#endif

    /*
     * Build the non-stop environment from the saved original:
     * - Clear all exception flags in status word
     * - Mask all exception traps in control word
     */
    tmp = *envp;
    tmp.__status_word = (unsigned short)(tmp.__status_word & ~FE_ALL_EXCEPT);
    tmp.__control_word = (unsigned short)(tmp.__control_word | FE_ALL_EXCEPT);

    /* Install the non-stop environment */
    __asm__ __volatile__("fldenv %0" : : "m"(tmp));

#ifdef __SSE__
    /* Clear MXCSR exception flags and mask all SSE exceptions */
    unsigned int mxcsr = envp->__mxcsr;
    mxcsr &= ~(unsigned int)MXCSR_EXCEPT_MASK; /* clear exception flags */
    mxcsr |= MXCSR_EMASK_MASK;                 /* mask all exceptions */
    __set_mxcsr(mxcsr);
#endif

    return 0;
}

/*
 * Update environment and re-raise exceptions.
 *
 * Saves currently raised exceptions, restores *envp, then re-raises
 * the saved exceptions into the restored environment.
 */
__attribute__((noinline))
int feupdateenv(const fenv_t *envp) {
    int pending = fetestexcept(FE_ALL_EXCEPT);

    /* Restore the environment (silently, without raising traps) */
    fesetenv(envp);

    /* Re-raise exceptions that were pending before the restore */
    if (pending)
        feraiseexcept(pending);

    return 0;
}

/* ============================================================
 * C23 additions
 * ============================================================ */

/* C23 7.6.2.5: set exception flags without raising traps.  Identical
 * to fesetexceptflag(&all_set, excepts) — but the C23 spelling takes
 * the bitmask directly. */
__attribute__((noinline))
int fesetexcept(int excepts) {
    fenv_t env;
    int mask = excepts & FE_ALL_EXCEPT;

    __asm__ __volatile__("fnstenv %0" : "=m"(env));
    env.__status_word = (unsigned int)((env.__status_word & ~(unsigned int)mask)
                                       | (unsigned int)mask);
    __asm__ __volatile__("fldenv %0" : : "m"(env));

#ifdef __SSE__
    unsigned int mxcsr = __get_mxcsr();
    mxcsr |= (unsigned int)mask;
    __set_mxcsr(mxcsr);
#endif
    return 0;
}

/* C23 7.6.2.7: test whether `excepts` bits are set in the SNAPSHOT
 * pointed at by `flagp` (NOT in the live FPU state).  Mirror of
 * fetestexcept but operating on a saved fexcept_t. */
__attribute__((noinline))
int fetestexceptflag(const fexcept_t *flagp, int excepts) {
    if (!flagp) return 0;
    int mask = excepts & FE_ALL_EXCEPT;
    return (int)(*flagp) & mask;
}

/* C23 7.6.4.1 / 7.6.4.2: control-mode get / set.  "Modes" are the
 * rounding direction plus (on x86) precision control and the
 * exception-mask bits — distinct from status (raised flags).
 *
 * We snapshot the x87 CW directly; the masking and rounding bits are
 * both inside that 16-bit word.  MXCSR's control half (rounding + masks
 * + FTZ/DAZ) is in the high bits we save separately. */
__attribute__((noinline))
int fegetmode(femode_t *modep) {
    if (!modep) return -1;
    unsigned short cw;
    __asm__ __volatile__("fnstcw %0" : "=m"(cw));
    modep->__control_word = cw;
#ifdef __SSE__
    modep->__mxcsr_control = __get_mxcsr() & 0xFFC0u;
    /* keep only the control bits (rounding + masks + FTZ/DAZ);
     * exception-status low 6 bits are NOT modes. */
#else
    modep->__mxcsr_control = 0;
#endif
    return 0;
}

__attribute__((noinline))
int fesetmode(const femode_t *modep) {
    if (modep == FE_DFL_MODE) {
        /* Default control word: precision 53-bit, round-to-nearest,
         * all exceptions masked.  Matches what fninit produces, but
         * without disturbing the status word. */
        unsigned short cw = 0x037F;
        __asm__ __volatile__("fldcw %0" : : "m"(cw));
#ifdef __SSE__
        unsigned int mxcsr = __get_mxcsr();
        mxcsr = (mxcsr & 0x003Fu) | 0x1F80u;  /* keep status, default modes */
        __set_mxcsr(mxcsr);
#endif
        return 0;
    }
    if (!modep) return -1;
    unsigned short cw = (unsigned short)modep->__control_word;
    __asm__ __volatile__("fldcw %0" : : "m"(cw));
#ifdef __SSE__
    unsigned int mxcsr = __get_mxcsr();
    mxcsr = (mxcsr & 0x003Fu) | (modep->__mxcsr_control & 0xFFC0u);
    __set_mxcsr(mxcsr);
#endif
    return 0;
}

/* C23 7.6.5: decimal-FP rounding direction.  Substrate has no
 * decimal FPU; this is in-memory state only, so callers that save +
 * restore it round-trip cleanly. */
static int g_dec_round = FE_DEC_TONEAREST;

__attribute__((noinline))
int fe_dec_getround(void) {
    return g_dec_round;
}

__attribute__((noinline))
int fe_dec_setround(int rdir) {
    switch (rdir) {
    case FE_DEC_TONEAREST:
    case FE_DEC_DOWNWARD:
    case FE_DEC_UPWARD:
    case FE_DEC_TOWARDZERO:
    case FE_DEC_TONEARESTFROMZERO:
        g_dec_round = rdir;
        return 0;
    default:
        return -1;
    }
}
