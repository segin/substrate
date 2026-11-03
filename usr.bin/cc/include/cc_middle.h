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
 * On i386 without SSE2, where libgcc's _Float16 routines (their halves in %xmm0) cannot be called: function
 * @p f's halves, if it only copies them -- loaded, stored, passed, taken -- made their bits, integers, as gcc
 * copies them; a conversion, an operation or a returned half refused, as gcc refuses it, with its message, at
 * the instruction's place or the function's.  Run before soft-float.  Nothing to do elsewhere.
 *
 * @param f     the function, rewritten in place
 * @param diag  receives an error
 * @return      0, or -1 with @p diag set
 */
int cc_i386_half_copies(cc_ssa_function_t *f, cc_diag_t *diag);

/** What softfloat_i386.c makes of i386 floating point (cc_softfloat_i386_mode()). */
typedef enum {
    CC_SOFTFLOAT_NONE = 0, /**< nothing: the x87 and SSE as ever */
    CC_SOFTFLOAT_ALL,      /**< -mno-80387 where the x87 would do the doubles: every operation soft-float */
    CC_SOFTFLOAT_RETURNS,  /**< -mno-fp-ret-in-387 alone: the x87 computes, results come back in %eax, %edx and
                                %ecx */
    CC_SOFTFLOAT_LDOUBLE   /**< -mno-80387 where SSE does the doubles: long double soft-float, results in the
                                general registers, and SSE the rest */
} cc_softfloat_mode_t;

/**
 * What i386 floating point is made of by softfloat_i386.c: soft-float
 * under -mno-80387 (-msoft-float, -mgeneral-regs-only) where the x87, not
 * SSE, would do the doubles, and only for long double where SSE does them;
 * under -mno-fp-ret-in-387 alone, results in the general registers.
 *
 * @return the mode, CC_SOFTFLOAT_NONE on another target
 */
cc_softfloat_mode_t cc_softfloat_i386_mode(void);

/**
 * i386 soft-float of one function (softfloat_i386.c), before legalization,
 * as cc_softfloat_i386_mode() says: every floating operation a call of
 * libgcc's soft-float routines, and floating values the bits of a double
 * (8 bytes) or the address of a long double's slot, passed and returned by
 * gcc's -mno-80387 ABI; or, of the values the x87 keeps, only the results
 * returned and received by that ABI.
 *
 * @param f     the function, rewritten in place
 * @param diag  receives an error
 * @return      0, or -1 (an operation it does not do, or out of memory)
 */
int cc_softfloat_i386_function(cc_ssa_function_t *f, cc_diag_t *diag);

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
 * Make each slot of @p f whose address does not escape, and which is only
 * loaded and stored whole at one size, a value: each store sets it (a
 * store of a wider integer truncated), each load reads it (a wider load
 * extended), and its STACKALLOC is left to dead-code elimination.  A
 * value lives in a register where the back end can keep it, where a slot
 * is reloaded at every use.  Run after cc_optimize_slots(), which reasons
 * about slots, has done its work (passes/mem.c).
 *
 * @param f  the function, rewritten in place
 * @return   1 if anything changed, 0 if not, -1 when out of memory
 */
int cc_promote_slots(cc_ssa_function_t *f);

/**
 * Read each copy `dst = src` of @p f as its source, where both are
 * defined once, neither's address is taken nor an asm's operand, and
 * they are of one class, size and signedness; the move is left to
 * dead-code elimination (passes/mem.c).
 *
 * @param f  the function, rewritten in place
 * @return   1 if anything changed, 0 if not, -1 when out of memory
 */
int cc_propagate_copies(cc_ssa_function_t *f);

/**
 * The bytes of @p f's slots that are reckoned to stay in memory, not to become values: those whose address
 * escapes, and those larger than a word (passes/mem.c).  What inlining @p f adds to a caller's frame.
 */
long cc_ssa_memory_slot_bytes(const cc_ssa_function_t *f);

/**
 * Call @p fn for each value instruction @p in reads, with the role it has: 0 an operand, 1 the address of a
 * load or store, 2 the value a store writes (passes/mem.c).  An operation's fields it does not read are not
 * named, whatever they hold.
 */
void cc_ssa_for_each_use(const cc_ssa_instr_t *in, void (*fn)(void *ctx, int v, int role), void *ctx);

/**
 * Coalesce `t = op ...; v = t`, t made once and read only by the move, into `v = op ...` (passes/mem.c):
 * cc lowers each update of a variable (x += k, p++) so, and the back end can then do it in place.
 *
 * @param f  the function, rewritten in place
 * @return   1 if anything changed, 0 if not, -1 when out of memory
 */
int cc_coalesce_moves(cc_ssa_function_t *f);

/**
 * Make each integer computation of @p f's loops that is the same on every trip -- its operands defined only
 * outside the loop, or by another such computation -- once, in the loop's preheader, innermost loops first
 * (passes/licm.c).  Only what cannot trap, reads no memory and every trip makes moves.
 *
 * @param f  the function, rewritten in place
 * @return   1 if anything moved, 0 if not, -1 when out of memory
 */
int cc_hoist_invariants(cc_ssa_function_t *f);

/** An interval of an integer value's content, read as signed, from lo to hi (passes/range.c). */
typedef struct {
    long long lo;
    long long hi;
} cc_range_t;

/**
 * What cc_fold_ranges() reports of each direct call it walks past: the call, and for each of its arguments
 * (call->arg_count of them) what it can hold there -- every value of 64 bits where nothing is known.
 */
typedef void (*cc_range_call_fn)(void *ctx, const cc_ssa_instr_t *call, const cc_range_t *args);

/**
 * Fold each integer compare of @p f whose operands' ranges decide it (passes/range.c): what each value can
 * hold, from its definitions, and where the stream is under a branch on it, from the branch -- Linux's min()
 * asks `(long long)len >= 0` of a `len` inside `while (len > 0)`, in a BUILD_BUG_ON.
 *
 * @param f        the function, rewritten in place
 * @param params   per parameter index, what every call passes it, or NULL where nothing is known
 * @param on_call  receives each direct call and its arguments' ranges, or NULL; every call is reported
 * @param ctx      passed to @p on_call
 * @return         1 when a compare was folded, 0 when none was, -1 when out of memory
 */
int cc_fold_ranges(cc_ssa_function_t *f, const cc_range_t *params, cc_range_call_fn on_call, void *ctx);

/**
 * Remove each definition of @p f that only makes its result -- a constant, a move, arithmetic, a compare --
 * where nothing reads the result before it is defined again or the function returns: a variable's
 * initializer overwritten before use (passes/mem.c).  Liveness over the blocks; nothing is removed in a
 * function with an asm goto or a computed goto, or of a value whose address is taken or an asm reads.
 *
 * @param f  the function, rewritten in place
 * @return   1 if anything was removed, 0 if not, -1 when out of memory
 */
int cc_dead_defs(cc_ssa_function_t *f);

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
 * @param once   per function of @p m: 1 if it is static and called from
 *               one place only, so it is inlined though not marked inline
 *               and at a larger size; may be NULL
 * @param opt_level  the optimization level, which a copy of a callee is
 *               folded at to weigh it where the call passes constants
 * @return       how many calls were inlined, or -1 when out of memory
 */
int cc_inline_calls(cc_ssa_module_t *m, size_t fi, const unsigned char *ready, const unsigned char *once,
                    int opt_level);

/**
 * Fold, propagate and drop what is dead in @p f, one function alone, as
 * the optimizer does each function at @p opt_level (passes/opt.c).
 *
 * @param f          the function, rewritten in place
 * @param opt_level  the optimization level
 * @return           0, or -1 when out of memory
 */
int cc_optimize_function(cc_ssa_function_t *f, int opt_level);

#endif
