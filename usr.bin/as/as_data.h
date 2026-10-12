#ifndef SUBSTRATE_AS_DATA_H
#define SUBSTRATE_AS_DATA_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The bytes a string literal says, without its quotes: `src` with its
 * escapes read as GNU as reads them.  0 with the bytes in *bytes_out
 * (which the caller frees; a NUL is put after them, and is not counted
 * in *len_out), or -1 for a `\` with nothing after it and for want of
 * memory.
 */
int as_decode_string_literal(const char *src, char **bytes_out, size_t *len_out);

/*
 * The LEB128 encoding of a 64-bit value, signed or not, into out (at
 * least 10 bytes).  The number of bytes written.
 */
size_t as_leb128_encode(long long value, int is_signed, unsigned char *out);

#ifdef __cplusplus
}
#endif

#endif
