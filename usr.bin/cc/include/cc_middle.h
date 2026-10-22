#ifndef CC_MIDDLE_H
#define CC_MIDDLE_H

#include "cc_frontend.h"
#include "cc_ssa.h"

int cc_run_middle_passes(cc_ssa_module_t *m, int opt_level, cc_diag_t *diag);
int cc_middle_legalize_module(cc_ssa_module_t *m, cc_diag_t *diag);

/**
 * i386 legalization of one function (legalize_i386.c): every operation on
 * a 64-bit integer becomes operations on its two 32-bit words, and a
 * 64-bit division a shift when the divisor is a constant power of two,
 * else a call of libgcc's __divdi3 or __udivdi3.
 *
 * @param f     the function, rewritten in place
 * @param diag  receives an error
 * @return      0, or -1 when out of memory
 */
int cc_legalize_i386_function(cc_ssa_function_t *f, cc_diag_t *diag);

/**
 * Fold the constant values of @p f (passes/opt.c), its branches left as
 * they are: legalization runs it at every -O level before a target splits
 * operations, so i386 computes `KTIME_MAX / NSEC_PER_SEC` at compile time,
 * as gcc does, rather than calling __divdi3, and 64-bit constants are not
 * taken apart into words to be put back together.
 *
 * @param f  the function
 * @return   0, or -1 when out of memory
 */
int cc_fold_constant_values(cc_ssa_function_t *f);

/**
 * What the optimizer learns of @p f's own stack slots whose addresses do
 * not escape (passes/mem.c): a slot stored once read as its value, a
 * structure slot split into its fields, and a compare of a slot only
 * stored constants decided by the set it can hold there.
 *
 * @param f  the function, rewritten in place
 * @return   1 if anything changed, 0 if not, -1 when out of memory
 */
int cc_optimize_slots(cc_ssa_function_t *f);

/**
 * Whether @p g may be inlined (passes/inline.c): static, inline by its
 * storage or always_inline and not noinline, small enough, its interface
 * plain values, and its body free of what depends on its own frame.
 *
 * @param g  the function
 * @return   1 if it may, else 0
 */
int cc_inline_candidate(const cc_ssa_function_t *g);

/**
 * Replace each call in function @p fi of @p m of a function that may be
 * inlined, and is marked in @p ready, by a copy of its body
 * (passes/inline.c).
 *
 * @param m      the module
 * @param fi     the function to rewrite
 * @param ready  per function of @p m: 1 if its body is final, its own
 *               calls inlined and folded, so it may be copied
 * @return       how many calls were inlined, or -1 when out of memory
 */
int cc_inline_calls(cc_ssa_module_t *m, size_t fi, const unsigned char *ready);

#endif
