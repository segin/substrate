/*
 * lib/c/src/sha256.c — SHA-256 primitive (FIPS 180-4).
 *
 * Compact reference implementation; pure ANSI C with no intrinsics
 * or platform asm.  Used by lib/c/src/crypt_sha256.c — not currently
 * exposed via a public header, but the symbols are extern so other
 * libc internals can pick them up if needed.
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct {
    uint32_t h[8];
    uint64_t length;     /* total bytes hashed */
    uint8_t  buf[64];
    size_t   buflen;
} sha256_ctx;

static const uint32_t K[64] = {
    0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,
    0x923f82a4u,0xab1c5ed5u,0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,
    0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,0xe49b69c1u,0xefbe4786u,
    0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
    0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,
    0x06ca6351u,0x14292967u,0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,
    0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,0xa2bfe8a1u,0xa81a664bu,
    0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
    0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,
    0x5b9cca4fu,0x682e6ff3u,0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,
    0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u
};

#define ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
#define CH(x,y,z)  (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x,y,z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define BSIG0(x) (ROR(x,2)  ^ ROR(x,13) ^ ROR(x,22))
#define BSIG1(x) (ROR(x,6)  ^ ROR(x,11) ^ ROR(x,25))
#define SSIG0(x) (ROR(x,7)  ^ ROR(x,18) ^ ((x) >> 3))
#define SSIG1(x) (ROR(x,17) ^ ROR(x,19) ^ ((x) >> 10))

void
__sha256_init(sha256_ctx *c)
{
    c->h[0] = 0x6a09e667u; c->h[1] = 0xbb67ae85u;
    c->h[2] = 0x3c6ef372u; c->h[3] = 0xa54ff53au;
    c->h[4] = 0x510e527fu; c->h[5] = 0x9b05688cu;
    c->h[6] = 0x1f83d9abu; c->h[7] = 0x5be0cd19u;
    c->length = 0;
    c->buflen = 0;
}

static void
sha256_compress(sha256_ctx *c, const uint8_t *block)
{
    uint32_t W[64];
    uint32_t a, b, d, e, f, g, h, t1, t2;
    uint32_t cc;
    int i;

    for (i = 0; i < 16; i++) {
        W[i] = ((uint32_t)block[i*4]     << 24) |
               ((uint32_t)block[i*4 + 1] << 16) |
               ((uint32_t)block[i*4 + 2] << 8)  |
               ((uint32_t)block[i*4 + 3]);
    }
    for (; i < 64; i++) {
        W[i] = SSIG1(W[i-2]) + W[i-7] + SSIG0(W[i-15]) + W[i-16];
    }

    a = c->h[0]; b = c->h[1]; cc = c->h[2]; d = c->h[3];
    e = c->h[4]; f = c->h[5]; g = c->h[6]; h = c->h[7];

    for (i = 0; i < 64; i++) {
        t1 = h + BSIG1(e) + CH(e, f, g) + K[i] + W[i];
        t2 = BSIG0(a) + MAJ(a, b, cc);
        h = g; g = f; f = e; e = d + t1;
        d = cc; cc = b; b = a; a = t1 + t2;
    }

    c->h[0] += a; c->h[1] += b; c->h[2] += cc; c->h[3] += d;
    c->h[4] += e; c->h[5] += f; c->h[6] += g; c->h[7] += h;
}

void
__sha256_update(sha256_ctx *c, const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    c->length += len;
    if (c->buflen != 0) {
        size_t need = 64 - c->buflen;
        size_t take = (len < need) ? len : need;
        memcpy(c->buf + c->buflen, p, take);
        c->buflen += take;
        p   += take;
        len -= take;
        if (c->buflen == 64) {
            sha256_compress(c, c->buf);
            c->buflen = 0;
        }
    }
    while (len >= 64) {
        sha256_compress(c, p);
        p   += 64;
        len -= 64;
    }
    if (len) {
        memcpy(c->buf, p, len);
        c->buflen = len;
    }
}

void
__sha256_final(sha256_ctx *c, uint8_t out[32])
{
    uint64_t bits = c->length * 8;
    int      i;

    /* Append 0x80, pad with zeros to 56 mod 64, append 8-byte BE len. */
    c->buf[c->buflen++] = 0x80;
    if (c->buflen > 56) {
        memset(c->buf + c->buflen, 0, 64 - c->buflen);
        sha256_compress(c, c->buf);
        c->buflen = 0;
    }
    memset(c->buf + c->buflen, 0, 56 - c->buflen);
    for (i = 0; i < 8; i++) {
        c->buf[56 + i] = (uint8_t)(bits >> ((7 - i) * 8));
    }
    sha256_compress(c, c->buf);
    for (i = 0; i < 8; i++) {
        out[i*4]     = (uint8_t)(c->h[i] >> 24);
        out[i*4 + 1] = (uint8_t)(c->h[i] >> 16);
        out[i*4 + 2] = (uint8_t)(c->h[i] >> 8);
        out[i*4 + 3] = (uint8_t)(c->h[i]);
    }
    memset(c, 0, sizeof(*c));
}
