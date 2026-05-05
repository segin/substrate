/*
 * fenv.h - Floating-point environment for i386 (C99/POSIX)
 *
 * This header defines the floating-point environment interface
 * for the i386 architecture, supporting both x87 and SSE (if available)
 * floating-point units.
 *
 * Compiler note: To allow the compiler to optimize FP operations correctly
 * while still respecting fenv state, add:
 *   #pragma STDC FENV_ACCESS ON
 * at the top of any translation unit that uses these interfaces.
 * In GCC, the equivalent is:
 *   #pragma GCC optimize ("no-fast-math")
 * or compile with -frounding-math and -fsignaling-nans as appropriate.
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
/* FE_DENORMAL is a non-standard extension (x87 DE flag); available unless
 * strict standards conformance is requested. */
#ifndef __STRICT_ANSI__
#define FE_DENORMAL   0x02    /* Denormal operand (non-standard extension) */
#endif
#define FE_DIVBYZERO  0x04    /* Divide by zero */
#define FE_OVERFLOW   0x08    /* Overflow */
#define FE_UNDERFLOW  0x10    /* Underflow */
#define FE_INEXACT    0x20    /* Inexact result */

#define FE_ALL_EXCEPT (FE_INVALID | FE_DIVBYZERO | \
                       FE_OVERFLOW | FE_UNDERFLOW | FE_INEXACT)

/*
 * x87 FPU Control Word rounding modes (bits 10-11)
 *
 * Values match the raw bit pattern stored in CW bits 10-11 (RC field)
 * and the MXCSR bits 13-14.
 */
#define FE_TONEAREST  0x00    /* Round to nearest (even) */
#define FE_DOWNWARD   0x01    /* Round downward (toward -infinity) */
#define FE_UPWARD     0x02    /* Round upward (toward +infinity) */
#define FE_TOWARDZERO 0x03    /* Round toward zero (truncate) */

/*
 * Floating-point environment type (x87 + optional SSE)
 *
 * Layout matches the 28-byte block written by fnstenv/fldenv,
 * plus a trailing __mxcsr field for SSE state.
 * Total size: 32 bytes.
 */
typedef struct {
    /* x87 FPU environment (fnstenv/fldenv 28-byte format) */
    unsigned short int __control_word;    /* Control word (CW) */
    unsigned short int __status_word;     /* Status word (SW) */
    unsigned short int __tag_word;        /* Tag word (TW) */
    unsigned int __ip_offset;             /* Instruction pointer offset (FIP) */
    unsigned short int __ip_selector;     /* Instruction pointer selector (FCS) */
    unsigned int __dp_offset;             /* Data pointer offset (FDP) */
    unsigned short int __dp_selector;     /* Data pointer selector (FDS) */
    unsigned int __mxcsr;                 /* MXCSR (SSE control/status register) */
} fenv_t;

/* Exception flag type (snapshot of status word exception bits) */
typedef unsigned short int fexcept_t;

/* Default environment pointer sentinel */
#define FE_DFL_ENV ((const fenv_t *)-1)

/*
 * Floating-point environment functions
 *
 * All functions are __attribute__((noinline)) to prevent the compiler
 * from reordering or eliding FPU state access.
 */

/* Clear specified exception flags */
int feclearexcept(int excepts) __attribute__((noinline));

/* Save current exception flags for specified exceptions */
int fegetexceptflag(fexcept_t *flagp, int excepts) __attribute__((noinline));

/* Raise specified exceptions (may trigger hardware traps if unmasked) */
int feraiseexcept(int excepts) __attribute__((noinline));

/* Set exception flags without raising traps */
int fesetexceptflag(const fexcept_t *flagp, int excepts) __attribute__((noinline));

/* Return bitwise OR of currently raised exceptions masked by excepts */
int fetestexcept(int excepts) __attribute__((noinline));

/* Get current rounding mode (returns FE_* constant) */
int fegetround(void) __attribute__((noinline));

/* Set rounding mode; returns 0 on success, non-zero for invalid rdir */
int fesetround(int rdir) __attribute__((noinline));

/* Save full floating-point environment */
int fegetenv(fenv_t *envp) __attribute__((noinline));

/* Restore floating-point environment without raising exceptions */
int fesetenv(const fenv_t *envp) __attribute__((noinline));

/* Save environment and enter non-stop (non-trapping) FP mode */
int feholdexcept(fenv_t *envp) __attribute__((noinline));

/* Restore environment and re-raise exceptions pending before the restore */
int feupdateenv(const fenv_t *envp) __attribute__((noinline));

#ifdef __cplusplus
}
#endif

#endif /* _FENV_H */
