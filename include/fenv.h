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
 * The first 28 bytes mirror the exact image fnstenv writes in 32-bit
 * protected mode.  Each x87 16-bit register sits in the LOW HALF of a
 * 32-bit slot, with the high half reserved — so __control_word,
 * __status_word, and __tag_word need 32-bit storage, not 16-bit, even
 * though only their low halves carry data.  An earlier version of
 * this struct packed the 16-bit fields back-to-back; that put
 * __status_word at offset 2 (where fnstenv writes the reserved upper
 * half of the control-word slot) and silently broke
 * feraiseexcept / fesetexceptflag / nearbyint.  The 32-bit-slot
 * layout below puts each field at the byte offset fnstenv actually
 * uses on this platform.
 *
 * Layout (matches Intel SDM Vol 1 §8.1.10 "Save FPU Environment"):
 *   offset 0:  control word slot   (CW in low 16)
 *   offset 4:  status word slot    (SW in low 16)
 *   offset 8:  tag word slot       (TW in low 16)
 *   offset 12: instruction pointer offset
 *   offset 16: FCS + opcode  (FCS in low 16)
 *   offset 20: data pointer offset
 *   offset 24: FDS slot
 *   offset 28: MXCSR  (SSE state, separate from the fnstenv block)
 */
typedef struct {
    unsigned int __control_word;          /* CW in low 16 bits */
    unsigned int __status_word;           /* SW in low 16 bits */
    unsigned int __tag_word;              /* TW in low 16 bits */
    unsigned int __ip_offset;             /* Instruction pointer offset */
    unsigned int __ip_sel_opcode;         /* FCS in low 16, opcode in high 16 */
    unsigned int __dp_offset;             /* Data pointer offset */
    unsigned int __dp_selector;           /* FDS in low 16 */
    unsigned int __mxcsr;                 /* MXCSR (SSE) */
} fenv_t;

/* Exception flag type (snapshot of status word exception bits) */
typedef unsigned short int fexcept_t;

/* C23: control-mode type.  Holds the subset of fenv_t that constitutes
 * "modes" (rounding direction, on x86 also precision control and the
 * exception-mask bits) — distinct from "status" (raised exception
 * flags).  Substrate stores the full x87 control word + MXCSR's
 * control half so fesetmode is a faithful round-trip. */
typedef struct {
    unsigned int __control_word;    /* x87 CW (low 16) */
    unsigned int __mxcsr_control;   /* MXCSR (low half holds control bits) */
} femode_t;

/* Default environment pointer sentinel */
#define FE_DFL_ENV ((const fenv_t *)-1)
/* C23: default mode pointer sentinel */
#define FE_DFL_MODE ((const femode_t *)-1)
/* C23: decimal-FP rounding directions — substrate has no decimal FP
 * hardware; we honour these enums as in-memory state only, but the
 * spelling is the standard one so consuming code compiles. */
#define FE_DEC_TONEAREST       0
#define FE_DEC_DOWNWARD        1
#define FE_DEC_UPWARD          2
#define FE_DEC_TOWARDZERO      3
#define FE_DEC_TONEARESTFROMZERO 4

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

/* C23 7.6.2.5: raise specified exceptions without trapping.  Like
 * feraiseexcept() but never asks the hardware to deliver a signal —
 * for code that wants to model the side effects of a future
 * computation. */
int fesetexcept(int excepts) __attribute__((noinline));

/* C23 7.6.2.7: test whether the specified exception flags are raised
 * IN THE SAVED SNAPSHOT, not in the live FPU state.  Symmetric to
 * fegetexceptflag's snapshot semantics. */
int fetestexceptflag(const fexcept_t *flagp, int excepts) __attribute__((noinline));

/* C23 7.6.4: get / set the dynamic floating-point control modes
 * (rounding direction + alternate-exception-handling configuration).
 * On x86 we save the x87 CW and the MXCSR control half. */
int fegetmode(femode_t *modep) __attribute__((noinline));
int fesetmode(const femode_t *modep) __attribute__((noinline));

/* C23 7.6.5: decimal-floating-point rounding direction.  Substrate
 * has no decimal FP hardware; these maintain the requested direction
 * as in-memory state so callers that just save / restore it
 * round-trip correctly. */
int fe_dec_getround(void) __attribute__((noinline));
int fe_dec_setround(int rdir) __attribute__((noinline));

#ifdef __cplusplus
}
#endif

#endif /* _FENV_H */
