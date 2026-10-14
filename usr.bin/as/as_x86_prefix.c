/*
 * What goes before an instruction that an encoder outside the table
 * writes: the segment override, the address-size prefix and lock, which
 * such an encoder is given the operands without.
 */
#include "as_x86_encode.h"

#include <string.h>

size_t as_x86_outer_prefixes(const as_x86_insn_t *insn, uint8_t out[AS_X86_OUTER_PREFIX_MAX]) {
    size_t n = 0;
    uint8_t seg = 0;

    if (insn == NULL || out == NULL) {
        return 0;
    }
    switch (insn->seg_override) {
    case AS_X86_SEG_CS: seg = 0x2e; break;
    case AS_X86_SEG_DS: seg = 0x3e; break;
    case AS_X86_SEG_ES: seg = 0x26; break;
    case AS_X86_SEG_FS: seg = 0x64; break;
    case AS_X86_SEG_GS: seg = 0x65; break;
    case AS_X86_SEG_SS: seg = 0x36; break;
    default: break;
    }
    if (seg != 0) {
        out[n++] = seg;
    }
    if (insn->address_size_override) {
        out[n++] = 0x67;
    }
    if (insn->lock_prefix) {
        out[n++] = 0xf0;
    }
    return n;
}

/*
 * The extension encoders were handed the start of the buffer, over
 * whatever prefixes had been written there: `lock cmpxchg16b (%rbx)`
 * lost its lock and was not atomic, and `popcnt %fs:(%rbx), %eax` and
 * every SSE3 to SSE4.2 instruction with a segment read from the wrong
 * place.
 */
int as_x86_encode_ext(as_x86_ext_encode_fn fn, const as_x86_insn_t *insn, uint8_t *out, size_t out_cap,
                      size_t *out_len, char *errbuf, size_t errbuf_sz) {
    uint8_t prefixes[AS_X86_OUTER_PREFIX_MAX];
    size_t n;
    size_t len = 0;

    if (fn == NULL || insn == NULL || out == NULL) {
        return -1;
    }
    /* Nor do they know {load} or {rex}; an instruction of theirs written
     * with one is refused rather than encoded without it. */
    if (insn->prefer_load || insn->rex_wanted) {
        return -1;
    }
    /* (An address through %bx or %si is theirs too: each goes to
     * as_x86_modrm16() for it.) */
    n = as_x86_outer_prefixes(insn, prefixes);
    if (n >= out_cap) {
        return -1;
    }
    if (fn(insn, out + n, out_cap - n, &len, errbuf, errbuf_sz) != 0) {
        return -1;
    }
    /* Nothing written is "not one of mine" from some of them. */
    if (len != 0) {
        memcpy(out, prefixes, n);
        len += n;
    }
    if (out_len != NULL) {
        *out_len = len;
    }
    return 0;
}
