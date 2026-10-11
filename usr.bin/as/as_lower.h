#ifndef SUBSTRATE_AS_LOWER_H
#define SUBSTRATE_AS_LOWER_H

#include "as_parser.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Rewrite the data directives that are other directives by another name
 * or with their arguments worked out -- .dc.*, .ds.*, .dcb.*, .octa,
 * .single, .tfloat, .string16 and its fellows, and .uleb128/.sleb128 of
 * numbers -- into .byte, .short, .long, .quad, .float, .double, .fill and
 * .space, which is all the rest of the assembler stores.  To be run on
 * what the parser gives.  address_bytes is 4 or 8, for .dc.a.
 */
int as_directives_lower(as_parse_result_t *parsed, unsigned address_bytes, char *errbuf, size_t errbuf_sz);

#ifdef __cplusplus
}
#endif

#endif
