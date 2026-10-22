#ifndef CC_OPTIONS_H
#define CC_OPTIONS_H

#include "cc_lang.h"
#include "cc_target.h"

/*
 * The settings one compilation runs with.  The driver fills a
 * cc_options_t from the command line and hands it to
 * cc_compile_c_to_s(), which selects it; every phase reads it back
 * with cc_options_current() rather than keeping a copy of its own.
 */

/** Scalar floating-point code on i386 (-mfpmath=). */
typedef enum {
    CC_FPMATH_AUTO = 0, /**< SSE2 when the target has it, else x87 */
    CC_FPMATH_SSE = 1,  /**< -mfpmath=sse */
    CC_FPMATH_387 = 2   /**< -mfpmath=387 */
} cc_fpmath_t;

/** The settings of one compilation. */
typedef struct {
    cc_lang_t lang;          /**< -std= */
    cc_target_t target;      /**< -m32 / -m64 */
    int code16;              /**< -m16: i386 code assembled under .code16gcc, for real mode (gcc) */
    int opt_level;           /**< -O0 .. -O3 */
    int debug;               /**< -g */
    int pic;                 /**< -fPIC, -fpic or -shared */
    int warn_all;            /**< -Wall */
    int warn_error;          /**< -Werror */
    int pedantic;            /**< -pedantic */
    int pedantic_errors;     /**< -pedantic-errors */
    int gnu89_inline;        /**< -f[no-]gnu89-inline: 1, 0, or -1 for the mode's default */
    int implicit_funcdecl;   /**< implicit function declarations: 1, 0, or -1 for the mode's default */
    int i386_isa_level;      /**< -march=: 3 (i386) .. 7; clamped to that range */
    int i386_sse2;           /**< -m[no-]sse2 */
    int i386_mmx;            /**< -m[no-]mmx */
    cc_fpmath_t i386_fpmath; /**< -mfpmath= */
    const char *dump_ir;     /**< -fdump-ir: the dump files' name without .ir; NULL for none */
    int dump_ir_all;         /**< -fdump-ir=all: one dump after each pass, not only the last */
} cc_options_t;

/**
 * Fill @p o with the defaults a bare `cc file.c` gets: gnu17, x86-64,
 * -O0, an i686 with MMX and SSE2 for -m32.
 *
 * @param o  the options to reset
 */
void cc_options_init(cc_options_t *o);

/**
 * Make @p o the settings of the compilation in progress, and select its
 * language mode and target.
 *
 * @param o  the settings; they are copied
 */
void cc_options_select(const cc_options_t *o);

/**
 * The settings of the compilation in progress: the defaults of
 * cc_options_init() until cc_options_select() says otherwise.
 *
 * @return the settings; never NULL
 */
const cc_options_t *cc_options_current(void);

/**
 * Whether GNU89 `inline` semantics apply: -fgnu89-inline, or by default
 * the GNU modes before C99.
 *
 * @return 1 if they do, else 0
 */
int cc_options_gnu89_inline(void);

/**
 * Whether a call to an undeclared function declares it implicitly: by
 * default in C90 and in the GNU modes, as gcc does.
 *
 * @return 1 if it does, else 0
 */
int cc_options_implicit_funcdecl(void);

#endif
