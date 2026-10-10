#ifndef SUBSTRATE_AS_RELOC_OP_H
#define SUBSTRATE_AS_RELOC_OP_H

/*
 * Relocation operators: the @GOTOFF of `sym@GOTOFF`.
 *
 * The one list of them.  The symbol table uses it to take an operator off
 * a name, so that the symbol is `sym`; the object writer uses it for the
 * relocation type the operator names.  There were two lists, of three
 * operators each, kept in step by hand.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The operator on `name`.  1, with the relocation type it names for
 * `machine` (EM_386 or EM_X86_64) in *type_out; 0 if `name` has no
 * operator; -1 if what follows its @ is not an operator, or not one that
 * machine has.  A versioned name (`f@@V1`) has no operator.
 */
int as_reloc_op_type(unsigned machine, const char *name, uint32_t *type_out);

/* Cut a known operator off `name`, leaving the symbol's own name: 1 if
 * one was cut, 0 if there was none.  Other uses of @ are left alone. */
int as_reloc_op_strip(char *name);

#ifdef __cplusplus
}
#endif

#endif
