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

#ifdef __cplusplus
}
#endif

#endif
