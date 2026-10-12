#ifndef SUBSTRATE_AS_LOWER_H
#define SUBSTRATE_AS_LOWER_H

#include "as_parser.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The widest alignment there is: 2 to this power, which is as wide as
 * the object library writes a section. */
#define AS_ALIGN_P2_MAX 20

/*
 * Rewrite the data directives that are other directives by another name
 * or with their arguments worked out -- .dc.*, .ds.*, .dcb.*, .octa,
 * .single, .tfloat, .string16 and its fellows, and .uleb128/.sleb128 of
 * numbers -- into .byte, .short, .long, .quad, .float, .double, .fill and
 * .space, which is all the rest of the assembler stores.  To be run on
 * what the parser gives.  address_bytes is 4 or 8, for .dc.a.
 *
 * It is also where the numbers of the other directives are settled: a
 * local common symbol is made storage, a symbol set to a number is put
 * for its name in an alignment, a count or a fill, an alignment is
 * validated, and what GNU as warns of in these is made a `.warning`
 * before the directive.
 */
int as_directives_lower(as_parse_result_t *parsed, unsigned address_bytes, char *errbuf, size_t errbuf_sz);

/*
 * What is ARM's or AArch64's own, made what the rest of the assembler
 * knows; to be run before as_directives_lower().
 *
 * `ldr reg, =value` is a move where one instruction can move the value,
 * and otherwise a load from a literal pool, which is put where `.ltorg`
 * or `.pool` is and, for what none of those took, at the end of its
 * section.  `.word` is four bytes, `.inst` an instruction's, `.align` a
 * power of two.  `.arm`, `.thumb` and `.code` are `.code32` and
 * `.code16`, by which the mode is known from there on.  `.syntax`,
 * `.arch`, `.cpu` and `.fpu` are checked and gone: every instruction
 * this assembler has is allowed.  `.eabi_attribute` is the section
 * `.ARM.attributes`.  The directives of ARM's unwinding tables are
 * refused, since no table is made.  An instruction in an IT block is
 * marked as being in one.
 *
 * thumb_funcs is given the names of the Thumb functions -- the label
 * after `.thumb_func`, and each label of Thumb code that `.type` calls a
 * function -- whose symbols have the low bit of their value set; the
 * caller frees them.  With `thumb` the source begins as Thumb, as if
 * `.thumb` were its first line.
 */
int as_arm_lower(as_parse_result_t *parsed, int a64, int thumb, char ***thumb_funcs, size_t *thumb_func_count,
                 char *errbuf, size_t errbuf_sz);

#ifdef __cplusplus
}
#endif

#endif
