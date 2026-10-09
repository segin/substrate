/*
 * elf_inflate.c - decompress a zlib stream.
 *
 * For sections stored compressed (SHF_COMPRESSED, ELFCOMPRESS_ZLIB), which
 * is how an assembler may write .debug_*: an Elf_Chdr and then a zlib
 * stream (RFC 1950) holding the section as it would otherwise be.  A
 * linker has to have the section itself -- the relocations against it
 * address the uncompressed bytes -- so it needs this much of zlib and no
 * more: inflate (RFC 1951) of a whole buffer into a buffer whose size is
 * known in advance from the header.
 *
 * Nothing is trusted: every read is bounded by the input, every write by
 * the output, and a stream that does not decode to exactly the expected
 * size, or whose Adler-32 does not match, is an error.
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "elf_private.h"

#define INF_MAXBITS     15
#define INF_MAXLCODES   286
#define INF_MAXDCODES   30
#define INF_FIXLCODES   288

struct inf_state {
    const uint8_t *in;
    size_t in_len, in_pos;
    uint8_t *out;
    size_t out_len, out_pos;
    uint32_t bitbuf;
    int bitcnt;
};

struct inf_huffman {
    uint16_t count[INF_MAXBITS + 1];    /* codes of each length */
    uint16_t symbol[INF_FIXLCODES];     /* symbols in code order */
};

/* `need` bits, least significant first; -1 if the input runs out. */
static int inf_bits(struct inf_state *s, int need) {
    uint32_t val = s->bitbuf;

    while (s->bitcnt < need) {
        if (s->in_pos >= s->in_len) {
            return -1;
        }
        val |= (uint32_t)s->in[s->in_pos++] << s->bitcnt;
        s->bitcnt += 8;
    }
    s->bitbuf = val >> need;
    s->bitcnt -= need;
    return (int)(val & ((1U << need) - 1U));
}

/* The canonical code for `n` symbols with the given lengths.  Returns 0 for
 * a complete code, negative for an over-subscribed one, positive for an
 * incomplete one. */
static int inf_construct(struct inf_huffman *h, const uint16_t *length, int n) {
    uint16_t offs[INF_MAXBITS + 1];
    int left = 1;

    memset(h->count, 0, sizeof(h->count));
    for (int sym = 0; sym < n; sym++) {
        h->count[length[sym]]++;
    }
    if (h->count[0] == n) {
        return 0;                       /* no codes: complete, and unusable */
    }
    for (int len = 1; len <= INF_MAXBITS; len++) {
        left <<= 1;
        left -= h->count[len];
        if (left < 0) {
            return left;
        }
    }
    offs[1] = 0;
    for (int len = 1; len < INF_MAXBITS; len++) {
        offs[len + 1] = (uint16_t)(offs[len] + h->count[len]);
    }
    for (int sym = 0; sym < n; sym++) {
        if (length[sym] != 0) {
            h->symbol[offs[length[sym]]++] = (uint16_t)sym;
        }
    }
    return left;
}

/* The next symbol, or negative: -1 out of input, -2 no such code. */
static int inf_decode(struct inf_state *s, const struct inf_huffman *h) {
    int code = 0, first = 0, index = 0;

    for (int len = 1; len <= INF_MAXBITS; len++) {
        int bit = inf_bits(s, 1);
        int count = h->count[len];

        if (bit < 0) {
            return -1;
        }
        code |= bit;
        if (code - count < first) {
            return h->symbol[index + (code - first)];
        }
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -2;
}

static int inf_codes(struct inf_state *s, const struct inf_huffman *lencode,
                     const struct inf_huffman *distcode) {
    static const uint16_t lens[29] = {
        3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
        35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
    static const uint16_t lext[29] = {
        0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
        3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
    static const uint16_t dists[30] = {
        1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
        257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145,
        8193, 12289, 16385, 24577 };
    static const uint16_t dext[30] = {
        0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
        7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };

    for (;;) {
        int symbol = inf_decode(s, lencode);

        if (symbol < 0) {
            return -1;
        }
        if (symbol < 256) {
            if (s->out_pos >= s->out_len) {
                return -1;
            }
            s->out[s->out_pos++] = (uint8_t)symbol;
        } else if (symbol == 256) {
            return 0;
        } else {
            int extra, len;
            size_t dist;

            symbol -= 257;
            if (symbol >= 29) {
                return -1;
            }
            extra = inf_bits(s, lext[symbol]);
            if (extra < 0) {
                return -1;
            }
            len = lens[symbol] + extra;

            symbol = inf_decode(s, distcode);
            if (symbol < 0 || symbol >= 30) {
                return -1;
            }
            extra = inf_bits(s, dext[symbol]);
            if (extra < 0) {
                return -1;
            }
            dist = (size_t)dists[symbol] + (size_t)extra;
            if (dist > s->out_pos || (size_t)len > s->out_len - s->out_pos) {
                return -1;
            }
            while (len-- > 0) {
                s->out[s->out_pos] = s->out[s->out_pos - dist];
                s->out_pos++;
            }
        }
    }
}

static int inf_stored(struct inf_state *s) {
    size_t len;

    s->bitbuf = 0;
    s->bitcnt = 0;
    if (s->in_len - s->in_pos < 4) {
        return -1;
    }
    len = (size_t)s->in[s->in_pos] | ((size_t)s->in[s->in_pos + 1] << 8);
    if (s->in[s->in_pos + 2] != (uint8_t)(~len & 0xff) ||
        s->in[s->in_pos + 3] != (uint8_t)((~len >> 8) & 0xff)) {
        return -1;
    }
    s->in_pos += 4;
    if (len > s->in_len - s->in_pos || len > s->out_len - s->out_pos) {
        return -1;
    }
    memcpy(s->out + s->out_pos, s->in + s->in_pos, len);
    s->in_pos += len;
    s->out_pos += len;
    return 0;
}

static int inf_fixed(struct inf_state *s) {
    struct inf_huffman lencode, distcode;
    uint16_t lengths[INF_FIXLCODES];
    int sym;

    for (sym = 0; sym < 144; sym++) lengths[sym] = 8;
    for (; sym < 256; sym++) lengths[sym] = 9;
    for (; sym < 280; sym++) lengths[sym] = 7;
    for (; sym < INF_FIXLCODES; sym++) lengths[sym] = 8;
    (void)inf_construct(&lencode, lengths, INF_FIXLCODES);
    for (sym = 0; sym < INF_MAXDCODES; sym++) lengths[sym] = 5;
    (void)inf_construct(&distcode, lengths, INF_MAXDCODES);
    return inf_codes(s, &lencode, &distcode);
}

static int inf_dynamic(struct inf_state *s) {
    static const uint8_t order[19] = {
        16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
    uint16_t lengths[INF_MAXLCODES + INF_MAXDCODES];
    struct inf_huffman lencode, distcode;
    int nlen, ndist, ncode, index, err;

    nlen = inf_bits(s, 5);
    ndist = inf_bits(s, 5);
    ncode = inf_bits(s, 4);
    if (nlen < 0 || ndist < 0 || ncode < 0) {
        return -1;
    }
    nlen += 257;
    ndist += 1;
    ncode += 4;
    if (nlen > INF_MAXLCODES || ndist > INF_MAXDCODES) {
        return -1;
    }
    memset(lengths, 0, sizeof(lengths));
    for (index = 0; index < ncode; index++) {
        int v = inf_bits(s, 3);

        if (v < 0) {
            return -1;
        }
        lengths[order[index]] = (uint16_t)v;
    }
    if (inf_construct(&lencode, lengths, 19) != 0) {
        return -1;                      /* the code-length code is complete */
    }

    index = 0;
    while (index < nlen + ndist) {
        int symbol = inf_decode(s, &lencode);

        if (symbol < 0) {
            return -1;
        }
        if (symbol < 16) {
            lengths[index++] = (uint16_t)symbol;
        } else {
            int len = 0, repeat;

            if (symbol == 16) {
                if (index == 0) {
                    return -1;
                }
                len = lengths[index - 1];
                repeat = inf_bits(s, 2);
                repeat = repeat < 0 ? -1 : repeat + 3;
            } else if (symbol == 17) {
                repeat = inf_bits(s, 3);
                repeat = repeat < 0 ? -1 : repeat + 3;
            } else {
                repeat = inf_bits(s, 7);
                repeat = repeat < 0 ? -1 : repeat + 11;
            }
            if (repeat < 0 || index + repeat > nlen + ndist) {
                return -1;
            }
            while (repeat-- > 0) {
                lengths[index++] = (uint16_t)len;
            }
        }
    }
    if (lengths[256] == 0) {
        return -1;                      /* no end-of-block code */
    }
    err = inf_construct(&lencode, lengths, nlen);
    if (err < 0 || (err > 0 && nlen - lencode.count[0] != 1)) {
        return -1;
    }
    err = inf_construct(&distcode, lengths + nlen, ndist);
    if (err < 0 || (err > 0 && ndist - distcode.count[0] != 1)) {
        return -1;
    }
    return inf_codes(s, &lencode, &distcode);
}

static uint32_t inf_adler32(const uint8_t *p, size_t n) {
    uint32_t a = 1, b = 0;

    while (n > 0) {
        size_t chunk = n > 5552 ? 5552 : n;

        n -= chunk;
        while (chunk-- > 0) {
            a += *p++;
            b += a;
        }
        a %= 65521U;
        b %= 65521U;
    }
    return (b << 16) | a;
}

/*
 * Inflate the zlib stream at `in` into exactly `out_len` bytes at `out`.
 * Returns 0, or -1 if the stream is malformed, truncated, of another
 * length, or fails its checksum.
 */
int elf__zlib_inflate(const uint8_t *in, size_t in_len, uint8_t *out,
                      size_t out_len) {
    struct inf_state s;
    int last, err = 0;

    if (in == NULL || (out == NULL && out_len != 0) || in_len < 6) {
        return -1;
    }
    /* CMF/FLG: deflate, a window of at most 32K, no preset dictionary,
     * and the pair a multiple of 31. */
    if ((in[0] & 0x0f) != 8 || (in[0] >> 4) > 7 || (in[1] & 0x20) != 0 ||
        (((unsigned)in[0] << 8) | in[1]) % 31U != 0) {
        return -1;
    }
    memset(&s, 0, sizeof(s));
    s.in = in + 2;
    s.in_len = in_len - 2 - 4;          /* less the header and the Adler-32 */
    s.out = out;
    s.out_len = out_len;

    do {
        int type;

        last = inf_bits(&s, 1);
        type = inf_bits(&s, 2);
        if (last < 0 || type < 0) {
            return -1;
        }
        err = type == 0 ? inf_stored(&s)
            : type == 1 ? inf_fixed(&s)
            : type == 2 ? inf_dynamic(&s) : -1;
    } while (err == 0 && !last);

    if (err != 0 || s.out_pos != out_len) {
        return -1;
    }
    in += in_len - 4;
    if (inf_adler32(out, out_len) !=
        (((uint32_t)in[0] << 24) | ((uint32_t)in[1] << 16) |
         ((uint32_t)in[2] << 8) | (uint32_t)in[3])) {
        return -1;
    }
    return 0;
}
