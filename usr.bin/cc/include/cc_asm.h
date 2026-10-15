#ifndef CC_ASM_H
#define CC_ASM_H

/*
 * GNU inline-asm operand constraints on x86: what a constraint string
 * such as "=r", "+m", "0" or "rm" lets an operand be.  Sema uses these
 * to check an asm statement, lowering and the emitter to place its
 * operands.
 */

/**
 * Whether a constraint contains a letter.
 *
 * @param c   the constraint, or NULL
 * @param ch  the letter
 * @return    1 if @p c contains @p ch, else 0 (and 0 for NULL)
 */
int cc_asm_constraint_has(const char *c, char ch);

/**
 * The operand a matching constraint ("0", "1", ...) ties this one to.
 *
 * @param c  the constraint, or NULL
 * @return   the operand number, or -1 if @p c is not all digits or is
 *           out of range
 */
int cc_asm_constraint_matched_operand(const char *c);

/**
 * Whether the operand may be placed in a general-purpose register:
 * r, q, a, b, c, d, S, D or g.  A missing constraint allows it.
 *
 * @param c  the constraint, or NULL
 * @return   1 if it may, else 0
 */
int cc_asm_constraint_allows_gpr(const char *c);

/**
 * Whether the constraint names any register class cc knows -- general,
 * byte, specific, SSE (x, y), x87 (t, u, f) or "any operand" (g, X) --
 * which is what sema accepts as a register constraint.  More than the
 * emitter can place: see cc_asm_constraint_allows_gpr().
 *
 * @param c  the constraint, or NULL
 * @return   1 if it does, else 0 (and 0 for NULL)
 */
int cc_asm_constraint_names_register(const char *c);

/**
 * Whether the operand may be a memory reference: m, o, V or g.
 *
 * @param c  the constraint, or NULL
 * @return   1 if it may, else 0 (and 0 for NULL)
 */
int cc_asm_constraint_allows_memory(const char *c);

/**
 * Whether the operand must be in memory: an m and no register letter
 * the emitter could use instead.
 *
 * @param c  the constraint, or NULL
 * @return   1 if it must, else 0 (and 0 for NULL)
 */
int cc_asm_constraint_is_memory_only(const char *c);

#endif
