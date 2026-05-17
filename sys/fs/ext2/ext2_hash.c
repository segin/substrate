/*
 * ext2_hash.c — directory-index name hashing.
 *
 * Port of FreeBSD's sys/fs/ext2fs/ext2_hash.c (Zheng Liu, 2010).
 * Implements the three hash variants ext4's htree directory index
 * uses: legacy, half-MD4, and TEA — each in signed-char and
 * unsigned-char flavours.  The caller passes a hash version from
 * the directory's htree root block (h_hash_version, 0..5) plus the
 * filesystem's hash seed (s_hash_seed[4] from the superblock).
 *
 * Output: a 32-bit major hash (used to bsearch the htree index
 * entries) and a 32-bit minor hash (used as the tiebreaker when
 * multiple entries land on the same major).
 *
 * Substrate keeps the algorithm bit-for-bit identical to FreeBSD;
 * differences would silently misroute lookups on real ext4 fs's.
 */
#include <stdint.h>
#include <string.h>
#include <ext2/ext2.h>

/* MD4 helpers (per the licensing notice in FreeBSD's source: this
 * portion is derived from RSA Data Security's MD4 Message-Digest
 * Algorithm and must retain its identification as such).  */
#define F(x, y, z) (((x) & (y)) | ((~x) & (z)))
#define G(x, y, z) (((x) & (y)) | ((x) & (z)) | ((y) & (z)))
#define H(x, y, z) ((x) ^ (y) ^ (z))
#define ROTATE_LEFT(x, n) (((x) << (n)) | ((x) >> (32 - (n))))

#define FF(a, b, c, d, x, s) { \
    (a) += F((b), (c), (d)) + (x); \
    (a) = ROTATE_LEFT((a), (s)); \
}
#define GG(a, b, c, d, x, s) { \
    (a) += G((b), (c), (d)) + (x) + (uint32_t)0x5A827999; \
    (a) = ROTATE_LEFT((a), (s)); \
}
#define HH(a, b, c, d, x, s) { \
    (a) += H((b), (c), (d)) + (x) + (uint32_t)0x6ED9EBA1; \
    (a) = ROTATE_LEFT((a), (s)); \
}

static void ext2_half_md4(uint32_t hash[4], uint32_t data[8]) {
    uint32_t a = hash[0], b = hash[1], c = hash[2], d = hash[3];
    FF(a, b, c, d, data[0],  3);
    FF(d, a, b, c, data[1],  7);
    FF(c, d, a, b, data[2], 11);
    FF(b, c, d, a, data[3], 19);
    FF(a, b, c, d, data[4],  3);
    FF(d, a, b, c, data[5],  7);
    FF(c, d, a, b, data[6], 11);
    FF(b, c, d, a, data[7], 19);
    GG(a, b, c, d, data[1],  3);
    GG(d, a, b, c, data[3],  5);
    GG(c, d, a, b, data[5],  9);
    GG(b, c, d, a, data[7], 13);
    GG(a, b, c, d, data[0],  3);
    GG(d, a, b, c, data[2],  5);
    GG(c, d, a, b, data[4],  9);
    GG(b, c, d, a, data[6], 13);
    HH(a, b, c, d, data[3],  3);
    HH(d, a, b, c, data[7],  9);
    HH(c, d, a, b, data[2], 11);
    HH(b, c, d, a, data[6], 15);
    HH(a, b, c, d, data[1],  3);
    HH(d, a, b, c, data[5],  9);
    HH(c, d, a, b, data[0], 11);
    HH(b, c, d, a, data[4], 15);
    hash[0] += a;
    hash[1] += b;
    hash[2] += c;
    hash[3] += d;
}

static void ext2_tea(uint32_t hash[4], uint32_t data[8]) {
    uint32_t tea_delta = 0x9E3779B9;
    uint32_t sum;
    uint32_t x = hash[0], y = hash[1];
    int n = 16;
    int i = 1;
    while (n-- > 0) {
        sum = i * tea_delta;
        x += ((y << 4) + data[0]) ^ (y + sum) ^ ((y >> 5) + data[1]);
        y += ((x << 4) + data[2]) ^ (x + sum) ^ ((x >> 5) + data[3]);
        i++;
    }
    hash[0] += x;
    hash[1] += y;
}

static uint32_t ext2_legacy_hash(const char *name, int len, int unsigned_char) {
    uint32_t h0, h1 = 0x12A3FE2D, h2 = 0x37ABE8F9;
    uint32_t multi = 0x6D22F5;
    const unsigned char *uname = (const unsigned char *)name;
    const signed char *sname = (const signed char *)name;
    int val, i;
    for (i = 0; i < len; i++) {
        if (unsigned_char) val = (uint32_t)*uname++;
        else               val = (int)*sname++;
        h0 = h2 + (h1 ^ (val * multi));
        if (h0 & 0x80000000u) h0 -= 0x7FFFFFFFu;
        h2 = h1;
        h1 = h0;
    }
    return (h1 << 1);
}

/* Chunk the name into fixed-size u32 buffers; pads with len-replicated
 * bytes when the name doesn't fill the chunk.  MD4 path uses 32-byte
 * chunks (dlen=32), TEA uses 16-byte chunks (dlen=16).  */
static void ext2_prep_hashbuf(const char *src, int slen, uint32_t *dst,
                              int dlen, int unsigned_char) {
    uint32_t padding = (uint32_t)slen | ((uint32_t)slen << 8) |
                       ((uint32_t)slen << 16) | ((uint32_t)slen << 24);
    uint32_t buf_val = padding;
    const unsigned char *ubuf = (const unsigned char *)src;
    const signed char *sbuf = (const signed char *)src;
    int len = (slen > dlen) ? dlen : slen;
    int buf_byte;
    int i;

    for (i = 0; i < len; i++) {
        if (unsigned_char) buf_byte = (uint32_t)ubuf[i];
        else               buf_byte = (int)sbuf[i];
        if ((i % 4) == 0) buf_val = padding;
        buf_val <<= 8;
        buf_val += buf_byte;
        if ((i % 4) == 3) {
            *dst++ = buf_val;
            dlen -= (int)sizeof(uint32_t);
            buf_val = padding;
        }
    }
    dlen -= (int)sizeof(uint32_t);
    if (dlen >= 0) *dst++ = buf_val;
    dlen -= (int)sizeof(uint32_t);
    while (dlen >= 0) {
        *dst++ = padding;
        dlen -= (int)sizeof(uint32_t);
    }
}

int ext2_htree_hash(const char *name, int len, const uint32_t *hash_seed,
                    int hash_version, uint32_t *hash_major,
                    uint32_t *hash_minor) {
    uint32_t hash[4];
    uint32_t data[8];
    uint32_t major = 0, minor = 0;
    int unsigned_char = 0;

    if (!name || !hash_major) return -1;
    if (len < 1 || len > 255)  goto error;

    hash[0] = 0x67452301;
    hash[1] = 0xEFCDAB89;
    hash[2] = 0x98BADCFE;
    hash[3] = 0x10325476;
    if (hash_seed) memcpy(hash, hash_seed, sizeof(hash));

    switch (hash_version) {
    case EXT2_HTREE_TEA_UNSIGNED:
        unsigned_char = 1;
        /* fallthrough */
    case EXT2_HTREE_TEA:
        while (len > 0) {
            ext2_prep_hashbuf(name, len, data, 16, unsigned_char);
            ext2_tea(hash, data);
            len  -= 16;
            name += 16;
        }
        major = hash[0];
        minor = hash[1];
        break;
    case EXT2_HTREE_LEGACY_UNSIGNED:
        unsigned_char = 1;
        /* fallthrough */
    case EXT2_HTREE_LEGACY:
        major = ext2_legacy_hash(name, len, unsigned_char);
        break;
    case EXT2_HTREE_HALF_MD4_UNSIGNED:
        unsigned_char = 1;
        /* fallthrough */
    case EXT2_HTREE_HALF_MD4:
        while (len > 0) {
            ext2_prep_hashbuf(name, len, data, 32, unsigned_char);
            ext2_half_md4(hash, data);
            len  -= 32;
            name += 32;
        }
        major = hash[1];
        minor = hash[2];
        break;
    default:
        goto error;
    }

    major &= ~1u;
    if (major == (uint32_t)(EXT2_HTREE_EOF << 1))
        major = (uint32_t)((EXT2_HTREE_EOF - 1) << 1);
    *hash_major = major;
    if (hash_minor) *hash_minor = minor;
    return 0;

error:
    *hash_major = 0;
    if (hash_minor) *hash_minor = 0;
    return -1;
}
