/*
 * lib/c/src/md5.c — MD5 primitive (RFC 1321).
 *
 * Compact little-endian implementation, no asm.  Exposed via
 * __md5_init/update/final for the $1$ crypt routine — not for new
 * cryptographic use (MD5 is broken).
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct {
    uint32_t h[4];
    uint64_t length;
    uint8_t  buf[64];
    size_t   buflen;
} md5_ctx;

#define ROL(x, n) (((x) << (n)) | ((x) >> (32 - (n))))
#define F(x,y,z) (((x) & (y)) | (~(x) & (z)))
#define G(x,y,z) (((x) & (z)) | ((y) & ~(z)))
#define H(x,y,z) ((x) ^ (y) ^ (z))
#define I(x,y,z) ((y) ^ ((x) | ~(z)))

#define STEP(f, a, b, c, d, x, t, s) do {           \
    (a) += f((b), (c), (d)) + (x) + (t);            \
    (a) = ROL((a), (s));                            \
    (a) += (b);                                     \
} while (0)

void
__md5_init(md5_ctx *m)
{
    m->h[0] = 0x67452301u; m->h[1] = 0xefcdab89u;
    m->h[2] = 0x98badcfeu; m->h[3] = 0x10325476u;
    m->length = 0;
    m->buflen = 0;
}

static void
md5_compress(md5_ctx *m, const uint8_t *block)
{
    uint32_t a = m->h[0], b = m->h[1], c = m->h[2], d = m->h[3];
    uint32_t x[16];
    int i;
    for (i = 0; i < 16; i++) {
        x[i] = (uint32_t)block[i*4]
             | ((uint32_t)block[i*4 + 1] << 8)
             | ((uint32_t)block[i*4 + 2] << 16)
             | ((uint32_t)block[i*4 + 3] << 24);
    }

    /* Round 1 */
    STEP(F, a, b, c, d, x[ 0], 0xd76aa478u,  7);
    STEP(F, d, a, b, c, x[ 1], 0xe8c7b756u, 12);
    STEP(F, c, d, a, b, x[ 2], 0x242070dbu, 17);
    STEP(F, b, c, d, a, x[ 3], 0xc1bdceeeu, 22);
    STEP(F, a, b, c, d, x[ 4], 0xf57c0fafu,  7);
    STEP(F, d, a, b, c, x[ 5], 0x4787c62au, 12);
    STEP(F, c, d, a, b, x[ 6], 0xa8304613u, 17);
    STEP(F, b, c, d, a, x[ 7], 0xfd469501u, 22);
    STEP(F, a, b, c, d, x[ 8], 0x698098d8u,  7);
    STEP(F, d, a, b, c, x[ 9], 0x8b44f7afu, 12);
    STEP(F, c, d, a, b, x[10], 0xffff5bb1u, 17);
    STEP(F, b, c, d, a, x[11], 0x895cd7beu, 22);
    STEP(F, a, b, c, d, x[12], 0x6b901122u,  7);
    STEP(F, d, a, b, c, x[13], 0xfd987193u, 12);
    STEP(F, c, d, a, b, x[14], 0xa679438eu, 17);
    STEP(F, b, c, d, a, x[15], 0x49b40821u, 22);

    /* Round 2 */
    STEP(G, a, b, c, d, x[ 1], 0xf61e2562u,  5);
    STEP(G, d, a, b, c, x[ 6], 0xc040b340u,  9);
    STEP(G, c, d, a, b, x[11], 0x265e5a51u, 14);
    STEP(G, b, c, d, a, x[ 0], 0xe9b6c7aau, 20);
    STEP(G, a, b, c, d, x[ 5], 0xd62f105du,  5);
    STEP(G, d, a, b, c, x[10], 0x02441453u,  9);
    STEP(G, c, d, a, b, x[15], 0xd8a1e681u, 14);
    STEP(G, b, c, d, a, x[ 4], 0xe7d3fbc8u, 20);
    STEP(G, a, b, c, d, x[ 9], 0x21e1cde6u,  5);
    STEP(G, d, a, b, c, x[14], 0xc33707d6u,  9);
    STEP(G, c, d, a, b, x[ 3], 0xf4d50d87u, 14);
    STEP(G, b, c, d, a, x[ 8], 0x455a14edu, 20);
    STEP(G, a, b, c, d, x[13], 0xa9e3e905u,  5);
    STEP(G, d, a, b, c, x[ 2], 0xfcefa3f8u,  9);
    STEP(G, c, d, a, b, x[ 7], 0x676f02d9u, 14);
    STEP(G, b, c, d, a, x[12], 0x8d2a4c8au, 20);

    /* Round 3 */
    STEP(H, a, b, c, d, x[ 5], 0xfffa3942u,  4);
    STEP(H, d, a, b, c, x[ 8], 0x8771f681u, 11);
    STEP(H, c, d, a, b, x[11], 0x6d9d6122u, 16);
    STEP(H, b, c, d, a, x[14], 0xfde5380cu, 23);
    STEP(H, a, b, c, d, x[ 1], 0xa4beea44u,  4);
    STEP(H, d, a, b, c, x[ 4], 0x4bdecfa9u, 11);
    STEP(H, c, d, a, b, x[ 7], 0xf6bb4b60u, 16);
    STEP(H, b, c, d, a, x[10], 0xbebfbc70u, 23);
    STEP(H, a, b, c, d, x[13], 0x289b7ec6u,  4);
    STEP(H, d, a, b, c, x[ 0], 0xeaa127fau, 11);
    STEP(H, c, d, a, b, x[ 3], 0xd4ef3085u, 16);
    STEP(H, b, c, d, a, x[ 6], 0x04881d05u, 23);
    STEP(H, a, b, c, d, x[ 9], 0xd9d4d039u,  4);
    STEP(H, d, a, b, c, x[12], 0xe6db99e5u, 11);
    STEP(H, c, d, a, b, x[15], 0x1fa27cf8u, 16);
    STEP(H, b, c, d, a, x[ 2], 0xc4ac5665u, 23);

    /* Round 4 */
    STEP(I, a, b, c, d, x[ 0], 0xf4292244u,  6);
    STEP(I, d, a, b, c, x[ 7], 0x432aff97u, 10);
    STEP(I, c, d, a, b, x[14], 0xab9423a7u, 15);
    STEP(I, b, c, d, a, x[ 5], 0xfc93a039u, 21);
    STEP(I, a, b, c, d, x[12], 0x655b59c3u,  6);
    STEP(I, d, a, b, c, x[ 3], 0x8f0ccc92u, 10);
    STEP(I, c, d, a, b, x[10], 0xffeff47du, 15);
    STEP(I, b, c, d, a, x[ 1], 0x85845dd1u, 21);
    STEP(I, a, b, c, d, x[ 8], 0x6fa87e4fu,  6);
    STEP(I, d, a, b, c, x[15], 0xfe2ce6e0u, 10);
    STEP(I, c, d, a, b, x[ 6], 0xa3014314u, 15);
    STEP(I, b, c, d, a, x[13], 0x4e0811a1u, 21);
    STEP(I, a, b, c, d, x[ 4], 0xf7537e82u,  6);
    STEP(I, d, a, b, c, x[11], 0xbd3af235u, 10);
    STEP(I, c, d, a, b, x[ 2], 0x2ad7d2bbu, 15);
    STEP(I, b, c, d, a, x[ 9], 0xeb86d391u, 21);

    m->h[0] += a; m->h[1] += b; m->h[2] += c; m->h[3] += d;
}

void
__md5_update(md5_ctx *m, const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    m->length += len;
    if (m->buflen) {
        size_t need = 64 - m->buflen;
        size_t take = (len < need) ? len : need;
        memcpy(m->buf + m->buflen, p, take);
        m->buflen += take;
        p   += take;
        len -= take;
        if (m->buflen == 64) {
            md5_compress(m, m->buf);
            m->buflen = 0;
        }
    }
    while (len >= 64) {
        md5_compress(m, p);
        p   += 64;
        len -= 64;
    }
    if (len) {
        memcpy(m->buf, p, len);
        m->buflen = len;
    }
}

void
__md5_final(md5_ctx *m, uint8_t out[16])
{
    uint64_t bits = m->length * 8;
    int      i;
    m->buf[m->buflen++] = 0x80;
    if (m->buflen > 56) {
        memset(m->buf + m->buflen, 0, 64 - m->buflen);
        md5_compress(m, m->buf);
        m->buflen = 0;
    }
    memset(m->buf + m->buflen, 0, 56 - m->buflen);
    for (i = 0; i < 8; i++) {
        m->buf[56 + i] = (uint8_t)(bits >> (i * 8));
    }
    md5_compress(m, m->buf);
    for (i = 0; i < 4; i++) {
        out[i*4]     = (uint8_t)(m->h[i]);
        out[i*4 + 1] = (uint8_t)(m->h[i] >> 8);
        out[i*4 + 2] = (uint8_t)(m->h[i] >> 16);
        out[i*4 + 3] = (uint8_t)(m->h[i] >> 24);
    }
    memset(m, 0, sizeof(*m));
}
