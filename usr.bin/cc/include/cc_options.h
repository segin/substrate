#ifndef CC_OPTIONS_H
#define CC_OPTIONS_H

#include "cc_lang.h"
#include "cc_target.h"

/*
 * The gcc version cc answers to (__GNUC__, --version, -dumpversion): the
 * gcc whose language and extensions it takes -- gcc 16's, the one Linux
 * is built with beside it: asm goto outputs (which Linux holds broken in
 * 13.0 to 13.2), __typeof_unqual__ (14), counted_by and
 * __builtin_counted_by_ref (15.1, on pointers 16), so that a kernel cc
 * configures takes the paths gcc's takes.
 */
#define CC_GCC_MAJOR 16
#define CC_GCC_MINOR 2
#define CC_GCC_PATCHLEVEL 1
#define CC_GCC_VERSION_STR "16.2.1"

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
    int pic;                 /**< -fPIC, -fpic, -fPIE, -fpie or -shared */
    int pie;                 /**< -fPIE or -fpie: the code is an executable's, whose own symbols bind within it */
    int warn_all;            /**< -Wall */
    int warn_error;          /**< -Werror */
    int pedantic;            /**< -pedantic */
    int pedantic_errors;     /**< -pedantic-errors */
    int gnu89_inline;        /**< -f[no-]gnu89-inline: 1, 0, or -1 for the mode's default */
    int implicit_funcdecl;   /**< implicit function declarations: 1, 0, or -1 for the mode's default */
    int i386_isa_level;      /**< -march=: 3 (i386) .. 7; clamped to that range */
    int i386_sse2;           /**< -m[no-]sse2 */
    int i386_mmx;            /**< -m[no-]mmx: 8-byte vectors in %mm registers, aligned to 8 */
    int i386_sse;            /**< SSE, from -march, -msse or -msse2: 16-byte vectors in %xmm registers */
    cc_fpmath_t i386_fpmath; /**< -mfpmath= */
    int i386_regparm;        /**< -mregparm=N: integer arguments in %eax, %edx, %ecx (0-3) */
    int i386_reg_struct_return; /**< -freg-struct-return: small structures returned in registers, not memory */
    const char *dump_ir;     /**< -fdump-ir: the dump files' name without .ir; NULL for none */
    int dump_ir_all;         /**< -fdump-ir=all: one dump after each pass, not only the last */
    int trivial_auto_var_init_zero; /**< -ftrivial-auto-var-init=zero: an automatic aggregate with no initializer is zeroed */
    int trivial_auto_var_init_pattern; /**< -ftrivial-auto-var-init=pattern: such an aggregate's every byte is 0xfe */
    int unwind_tables;       /**< -f[no-]asynchronous-unwind-tables, -f[no-]unwind-tables: frame descriptions in .eh_frame (the default), else .debug_frame */
    int i386_stack_boundary; /**< -mpreferred-stack-boundary=N: the stack's alignment at a call, 2^N bytes; 0 for the psABI's 16 */
    int cmodel_kernel;       /**< -mcmodel=kernel (x86-64): the code and its data in the top 2 GiB, linked with no GOT */
    int omit_frame_pointer;  /**< -fomit-frame-pointer: a function the frame pointer is not needed in addresses its
                                  frame off the stack pointer and has %ebp (%rbp) to allocate */
    int no_sse;              /**< -mno-sse, the last of it and -msse: no SSE register is touched where none is used */
    int no_jump_tables;      /**< -fno-jump-tables, the last of it and -fjump-tables: a switch is always compares --
                                  Linux's code that runs before it is relocated, which a table's absolute entries
                                  would send nowhere */
    int wrapv;               /**< -fwrapv or -fno-strict-overflow, the last of them and -fno-wrapv, -fstrict-overflow:
                                  signed arithmetic wraps, as Linux builds; else its overflow is undefined, as C has
                                  it, and an int's sum is not extended again */
    int syntax_only;         /**< -fsyntax-only: the source checked, and nothing written */
    int cf_branch;           /**< -fcf-protection=branch or full: an endbr where an indirect branch may land --
                                  each function's entry and each label whose address is taken (Intel CET IBT) */
    int indirect_thunk;      /**< -mindirect-branch=thunk-extern: an indirect call or jump is one through
                                  __x86_indirect_thunk_<reg>, the retpoline the program supplies (Linux's), its
                                  target in that register */
    int indirect_cs_prefix;  /**< -mindirect-branch-cs-prefix: such a call or jump through %r8..%r15 has a cs
                                  prefix, the byte Linux's rewriting of it in place needs */
    int return_thunk;        /**< -mfunction-return=thunk-extern: a return is a jump to __x86_return_thunk, the
                                  program's (Linux's, with CONFIG_MITIGATION_RETHUNK) */
    int harden_sls;          /**< -mharden-sls=: an int3 after each return (1) and indirect jump (2), where the
                                  processor may run on straight past it (Linux's CONFIG_MITIGATION_SLS) */
    int profile;             /**< -pg: a call at each function's entry, 0 none, 1 to mcount once the frame is
                                  made, 2 to __fentry__ before anything (-mfentry, x86-64's default) -- Linux's
                                  ftrace patches the latter */
    int record_mcount;       /**< -mrecord-mcount: each such call's address in section __mcount_loc */
    int nop_mcount;          /**< -mnop-mcount: the call a 5-byte nop, for the program to make one later */
    int patchable_count;     /**< -fpatchable-function-entry=N,M: N nops at each function's entry, for the
                                  program to patch -- Linux's call padding */
    int patchable_before;    /**< M of them before the entry */
    int min_function_alignment; /**< -fmin-function-alignment=N: each function's code starts on N bytes (with
                                     -fpatchable-function-entry, its nops before it do); 0 for none */
    int unsigned_char;       /**< -funsigned-char: plain char is unsigned, as Linux builds (it is signed on x86) */
    int short_wchar;         /**< -fshort-wchar: wchar_t is unsigned short, an L"" literal's elements 2 bytes
                                  (Linux's EFI code's UTF-16) */
    int stack_protector;     /**< -fstack-protector*: 0 none, 1 (the functions with a char array of 8 bytes or
                                  more, or alloca), 2 -strong (with any array, or a local whose address is taken),
                                  3 -all, 4 -explicit (only those declared stack_protect) */
    int ssp_guard_global;    /**< -mstack-protector-guard=global: the canary is __stack_chk_guard, not the
                                  thread's (tls, the default) */
    int ssp_guard_seg;       /**< -mstack-protector-guard-reg=fs|gs: CC_SSA_SEG_FS or CC_SSA_SEG_GS; 0 for the
                                  target's (%fs on x86-64, %gs on i386) */
    long ssp_guard_offset;   /**< -mstack-protector-guard-offset=N; -1 for the target's (40, 20) */
    const char *ssp_guard_symbol; /**< -mstack-protector-guard-symbol=S: the canary at S in the segment (Linux's
                                       __ref_stack_chk_guard); NULL for the offset alone */
    int default_visibility;  /**< -fvisibility=: what a definition with none of its own has, as CC_LEX_VIS_*:
                                  default (0), hidden (1), protected (2), internal (3) */
    int no_80387;            /**< -mno-80387: no x87 instruction; floating point that needs one is refused */
    int no_fp_ret_in_387;    /**< -mno-fp-ret-in-387: no result in %st0; a function or call with one is refused */
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
