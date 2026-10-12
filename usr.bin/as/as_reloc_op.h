#ifndef SUBSTRATE_AS_RELOC_OP_H
#define SUBSTRATE_AS_RELOC_OP_H

/*
 * Relocation operators: the @GOTOFF of `sym@GOTOFF`.
 *
 * The one list of them, which the object writer asks for the relocation
 * type an operator names.  There were two lists, of three operators
 * each, kept in step by hand.
 *
 * The operator is no part of the symbol's name: the expression parser
 * takes it off where it reads the symbol (as_expr_t.reloc_op), and it is
 * handed here by itself.  It was left in the name, and found again by
 * its `@` by everything the name passed through.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The operator `op`, as it was written and without its @.  1, with the
 * relocation type it names for `machine` (EM_386 or EM_X86_64) in
 * *type_out; 0 if `op` is NULL, there being none; -1 if it is not an
 * operator, or not one that machine has.
 */
int as_reloc_op_type(unsigned machine, const char *op, uint32_t *type_out);

/* The type an operator names where the field is of 64 bits: `.quad
 * foo@GOT` is R_X86_64_GOT64 where `.long foo@GOT` is R_X86_64_GOT32.
 * `type` itself where there is no wider one. */
uint32_t as_reloc_op_widen(unsigned machine, uint32_t type);

#ifdef __cplusplus
}
#endif

#endif
