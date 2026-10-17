/*
 * types.c: facts about C types that every phase of the compiler needs.
 *
 * This is the one place the size and alignment of a scalar type are
 * decided; the parser (structure layout, sizeof, _Alignof), sema,
 * lowering and the emitter all ask here.
 */

#include "cc_types.h"

#include <ctype.h>
#include <limits.h>
#include <string.h>

/**
 * Look up the layout of a basic type on a target.
 *
 * @param tgt   the target
 * @param kind  the type's kind
 * @param out   receives the size and alignment
 * @return      1 if @p kind is a basic arithmetic or enumerated kind, 0 if not
 */
static int scalar_layout(const cc_target_desc_t *tgt, cc_ctype_kind_t kind, cc_target_scalar_layout_t *out) {
    unsigned char ptr = (unsigned char)tgt->pointer_size;

    switch (kind) {
    case CC_CT_BOOL:
    case CC_CT_CHAR:
    case CC_CT_SCHAR:
    case CC_CT_UCHAR:
        out->size = 1;
        out->align = 1;
        return 1;
    case CC_CT_SHORT:
    case CC_CT_USHORT:
    case CC_CT_FLOAT16:
        out->size = 2;
        out->align = 2;
        return 1;
    case CC_CT_INT:
    case CC_CT_UINT:
    case CC_CT_FLOAT:
    case CC_CT_ENUM:
    case CC_CT_DECIMAL32:
        out->size = 4;
        out->align = 4;
        return 1;
    case CC_CT_LONG:
    case CC_CT_ULONG:
        out->size = ptr;
        out->align = ptr;
        return 1;
    case CC_CT_LLONG:
    case CC_CT_ULLONG:
        *out = tgt->int64;
        return 1;
    case CC_CT_DOUBLE:
        *out = tgt->float64;
        return 1;
    case CC_CT_LDOUBLE:
        *out = tgt->long_double;
        return 1;
    case CC_CT_DECIMAL64:
        *out = tgt->decimal64;
        return 1;
    case CC_CT_DECIMAL128:
        *out = tgt->decimal128;
        return 1;
    default:
        return 0;
    }
}

/**
 * Size of a basic type on the target being compiled for.
 *
 * @param kind  the type's kind
 * @return      its size in bytes, or -1 if @p kind is not a basic kind
 */
long cc_scalar_size(cc_ctype_kind_t kind) {
    cc_target_scalar_layout_t l;

    if (!scalar_layout(cc_target_current(), kind, &l)) {
        return -1;
    }
    return l.size;
}

/**
 * Alignment of a basic type on the target being compiled for.
 *
 * @param kind  the type's kind
 * @return      its alignment in bytes, or -1 if @p kind is not a basic kind
 */
long cc_scalar_align(cc_ctype_kind_t kind) {
    cc_target_scalar_layout_t l;

    if (!scalar_layout(cc_target_current(), kind, &l)) {
        return -1;
    }
    return l.align;
}

/**
 * Size of `_BitInt(N)` on the target being compiled for.
 *
 * @param width  N, the width in bits
 * @return       its size in bytes, or -1 if @p width is not positive
 */
long cc_bitint_size(long width) {
    long word = cc_target_current()->pointer_size;

    if (width <= 0) {
        return -1;
    }
    if (width <= 8) {
        return 1;
    }
    if (width <= 16) {
        return 2;
    }
    if (width <= 32) {
        return 4;
    }
    if (width <= 64) {
        return 8;
    }
    return (width + word * 8 - 1) / (word * 8) * word;
}

/**
 * Whether a string literal expression is a wide one.
 *
 * @param e  a CC_EXPR_STR expression
 * @return   1 if it is wide, else 0
 */
int cc_string_literal_is_wide(const cc_expr_t *e) {
    return cc_string_literal_elem(e)->kind != CC_CT_CHAR;
}

/**
 * The element type of a string literal: char, or the character type of
 * a wide one (char16_t, char32_t or wchar_t's type).
 *
 * @param e  a CC_EXPR_STR expression
 * @return   the type; char for anything else
 */
const cc_ctype_t *cc_string_literal_elem(const cc_expr_t *e) {
    if (e == NULL || e->kind != CC_EXPR_STR || e->aux_ctype == NULL) {
        return cc_ct_basic(CC_CT_CHAR);
    }
    return e->aux_ctype;
}

/**
 * The length of the UTF-8 sequence a byte starts.
 *
 * @param b0  the first byte
 * @return    1 to 4; 1 for a byte no sequence starts with
 */
static size_t utf8_seq_len(unsigned char b0) {
    if ((b0 & 0x80u) == 0) return 1;
    if ((b0 & 0xE0u) == 0xC0u) return 2;
    if ((b0 & 0xF0u) == 0xE0u) return 3;
    if ((b0 & 0xF8u) == 0xF0u) return 4;
    return 1;
}

/**
 * The number of elements of the array a string literal is.  See
 * cc_types.h.
 *
 * @param e  a CC_EXPR_STR expression
 * @return   the bound, or 0
 */
long cc_string_literal_units(const cc_expr_t *e) {
    const unsigned char *s;
    size_t n;
    size_t i;
    size_t units;
    int wide;
    int utf16;

    if (e == NULL || e->kind != CC_EXPR_STR || e->ident == NULL) {
        return 0;
    }
    s = (const unsigned char *)e->ident;
    n = strlen(e->ident);
    if (n < 2 || s[0] != '"' || s[n - 1] != '"') {
        return 0;
    }
    wide = cc_string_literal_is_wide(e);
    utf16 = cc_string_literal_elem(e)->kind == CC_CT_USHORT;
    i = 1;
    n -= 1;
    units = 0;
    while (i < n) {
        if (s[i] == '\\') {
            i++;
            if (i >= n) {
                return 0;
            }
            if (s[i] == 'x') {
                i++;
                while (i < n && isxdigit(s[i])) {
                    i++;
                }
            } else if (s[i] >= '0' && s[i] <= '7') {
                int k = 0;
                while (i < n && k < 3 && s[i] >= '0' && s[i] <= '7') {
                    i++;
                    k++;
                }
            } else if (s[i] == 'u' || s[i] == 'U') {
                /* a universal character name: one character, encoded as the literal's element type encodes it */
                int digits = s[i] == 'u' ? 4 : 8;
                unsigned long cp = 0;
                int k;

                i++;
                for (k = 0; k < digits && i < n && isxdigit(s[i]); ++k, ++i) {
                    cp = cp * 16 + (unsigned long)(isdigit(s[i]) ? s[i] - '0' : (tolower(s[i]) - 'a' + 10));
                }
                if (!wide) {
                    units += cp < 0x80 ? 1 : cp < 0x800 ? 2 : cp < 0x10000 ? 3 : 4;
                } else {
                    units += (utf16 && cp > 0xFFFF) ? 2 : 1;
                }
                continue;
            } else {
                i++;
            }
            units++;
            continue;
        }
        if (!wide) {
            i++;
            units++;
            continue;
        }
        {
            /* a source character in UTF-8: one element, or a surrogate pair in UTF-16 beyond the BMP */
            size_t adv = utf8_seq_len(s[i]);
            if (adv == 0) adv = 1;
            if (i + adv > n) adv = 1;
            i += adv;
            units += (utf16 && adv == 4) ? 2 : 1;
        }
    }
    if (units >= (size_t)LONG_MAX) {
        return 0;
    }
    return (long)units + 1;
}

/**
 * Alignment of `_BitInt(N)` on the target being compiled for.
 *
 * @param width  N, the width in bits
 * @return       its alignment in bytes, or -1 if @p width is not positive
 */
long cc_bitint_align(long width) {
    long size = cc_bitint_size(width);
    long word = cc_target_current()->pointer_size;

    if (size < 0) {
        return -1;
    }
    return size < word ? size : word;
}
