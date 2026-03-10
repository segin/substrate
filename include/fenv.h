/*
 * fenv.h - Floating-point environment for i386 (C99/POSIX)
 *
 * This header defines the floating-point environment interface
 * for the i386 architecture, supporting both x87 and SSE (if available)
 * floating-point units.
 */

#ifndef _FENV_H
#define _FENV_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * x87 FPU Status Word exception bits (bits 0-5)
 *
 * See Intel IA-32 manual for details
 */
#define FE_INVALID    0x01    /* Invalid operation */
#define FE_DENORMAL   0x02    /* Denormal operand */
#define FE_DIVBYZERO  0x04    /* Divide by zero */
#define FE_OVERFLOW   0x08    /* Overflow */
#define FE_UNDERFLOW  0x10    /* Underflow */
#define FE_INEXACT    0x20    /* Inexact result */

#define FE_ALL_EXCEPT (FE_INVALID | FE_DENORMAL | FE_DIVBYZERO | \
                       FE_OVERFLOW | FE_UNDERFLOW | FE_INEXACT)

/*
 * x87 FPU Control Word rounding modes (bits 10-11)
 */
#define FE_TONEAREST  0x00    /* Round to nearest (even) */
#define FE_DOWNWARD   0x01    /* Round downward (toward -infinity) */
#define FE_UPWARD     0x02    /* Round upward (toward +infinity) */
#define FE_TOWARDZERO 0x03    /* Round toward zero (truncate) */

/*
 * Floating-point environment type (x87 + optional SSE)
 *
 * x87 environment size: 28 bytes (fnstenv/fldenv format)
 * SSE MXCSR: 4 bytes (if SSE is supported)
 */
typedef struct {
    /* x87 FPU environment (fnstenv/fldenv format) */
    unsigned short int __control_word;    /* Control word (cw) */
    unsigned short int __status_word;     /* Status word (sw) */
    unsigned short int __tag_word;        /* Tag word (tw) */
    unsigned int __ip_offset;             /* Instruction pointer offset (fip) */
    unsigned short int __ip_selector;     /* Instruction pointer selector (fcs) */
    unsigned int __dp_offset;             /* Data pointer offset (fdp) */
    unsigned short int __dp_selector;     /* Data pointer selector (fds) */
    unsigned int __mxcsr;                 /* MXCSR (SSE control/status register) */
} fenv_t;

/* Exception flag type (snapshot of status word exception bits) */
typedef unsigned short int fexcept_t;

/* Default environment pointer */
#define FE_DFL_ENV ((const fenv_t *)-1)

/*
 * Floating-point environment functions
 *
 * All functions are __attribute__((noinline)) to prevent
 * compiler from reordering FPU state operations
 */

/* Clear specified exceptions */
int feclearexcept(int excepts) __attribute__((noinline));

/* Get exception flags */
int fegetexceptflag(fexcept_t *flagp, int excepts) __attribute__((noinline));

/* Raise specified exceptions */
int feraiseexcept(int excepts) __attribute__((noinline));

/* Set exception flags */
int fesetexceptflag(const fexcept_t *flagp, int excepts) __attribute__((noinline));

/* Test if any of the specified exceptions are raised */
int fetestexcept(int excepts) __attribute__((noinline));

/* Get current rounding mode */
int fegetround(void) __attribute__((noinline));

/* Set rounding mode */
int fesetround(int rdir) __attribute__((noinline));

/* Get current environment */
int fegetenv(fenv_t *envp) __attribute__((noinline));

/* Set environment */
int fesetenv(const fenv_t *envp) __attribute__((noinline));

/* Hold exceptions (enter non-stop mode) */
int feholdexcept(fenv_t *envp) __attribute__((noinline));

/* Update environment and re-raise exceptions */
int feupdateenv(const fenv_t *envp) __attribute__((noinline));

#ifdef __cplusplus
}
#endif

#endif /* _FENV_H */
