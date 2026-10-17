#ifndef CC_IRBUILD_H
#define CC_IRBUILD_H

/*
 * cc_irbuild.h: appending instructions to an SSA function.
 *
 * Lowering builds a function's IR with these, and so do the per-target
 * passes that write IR of their own (the ABI's va_arg sequences).  Each
 * appended instruction records its value's width and extension, so a
 * caller never has to.
 */

#include "cc_diag.h"
#include "cc_ssa.h"

/**
 * Append an instruction to @p f.  A value it defines gets the width and
 * extension its opcode gives it (a load its access size, a compare 4
 * bytes, an address the pointer size, ...).
 *
 * @param f   the function
 * @param in  the instruction, which @p f now owns
 * @return    0, or -1 when out of memory
 */
int cc_ir_push(cc_ssa_function_t *f, cc_ssa_instr_t in);

/**
 * Add a value to @p f.
 *
 * @param f   the function
 * @param vt  its register class
 * @return    the value, or -1 when out of memory
 */
int cc_ir_new_value(cc_ssa_function_t *f, cc_value_type_t vt);

/**
 * Record how many bytes a value holds.
 *
 * @param f     the function the value belongs to
 * @param v     the value
 * @param size  its size in bytes -- a structure value's whole size; 0 or
 *              less means "a word", the pointer size
 */
void cc_ir_set_size(cc_ssa_function_t *f, int v, long size);

/**
 * A new label in @p f, not yet placed.
 *
 * @param f  the function
 * @return   the label
 */
int cc_ir_new_label(cc_ssa_function_t *f);

/**
 * Append a STACKALLOC: @p total_size bytes in the frame.
 *
 * @param sf          the function
 * @param total_size  bytes; more than 0
 * @param diag        receives an error
 * @return            the storage's address, or -1
 */
int cc_ir_stackalloc(cc_ssa_function_t *sf, long total_size, cc_diag_t *diag);

/**
 * Place @p label here.
 *
 * @param sf     the function
 * @param label  a label from cc_ir_new_label()
 * @return       0, or -1 when out of memory
 */
int cc_ir_label(cc_ssa_function_t *sf, int label);

/**
 * Append a jump to @p label.
 *
 * @param sf     the function
 * @param label  where to go
 * @return       0, or -1 when out of memory
 */
int cc_ir_br(cc_ssa_function_t *sf, int label);

/**
 * Append a conditional jump.
 *
 * @param sf           the function
 * @param cond         the condition value; nonzero is true
 * @param label_true   where to go when it is true
 * @param label_false  where to go when it is not
 * @return             0, or -1 when out of memory
 */
int cc_ir_br_cond(cc_ssa_function_t *sf, int cond, int label_true, int label_false);

/**
 * Append a TRAP.
 *
 * @param sf  the function
 * @return    0, or -1 when out of memory
 */
int cc_ir_trap(cc_ssa_function_t *sf);

/**
 * Append `dst = src`.
 *
 * @param sf   the function
 * @param dst  the value written
 * @param src  the value read
 * @return     0, or -1 when out of memory
 */
int cc_ir_mov(cc_ssa_function_t *sf, int dst, int src);

/**
 * Append an integer constant.
 *
 * @param sf  the function
 * @param v   the constant
 * @return    its value, or -1 when out of memory
 */
int cc_ir_const(cc_ssa_function_t *sf, long long v);

#endif
