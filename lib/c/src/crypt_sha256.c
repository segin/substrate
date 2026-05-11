/*
 * lib/c/src/crypt_sha256.c — SHA-256 crypt ($5$).
 *
 * Implements Ulrich Drepper's "Unix crypt using SHA-256" algorithm,
 * as used by glibc and most modern Unixes.  Output format:
 *
 *     $5$[rounds=<N>$]<salt>$<43-char-base64>
 *
 * Default rounds = 5000.  Salt is up to 16 chars, base64 alphabet
 * "./0-9A-Za-z".  Reference: U. Drepper, "Unix crypt using SHA-256
 * and SHA-512" (2007), and POSIX-compliant implementations in
 * musl/glibc/openbsd.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint32_t h[8];
    uint64_t length;
    uint8_t  buf[64];
    size_t   buflen;
} sha256_ctx;
extern void __sha256_init(sha256_ctx *);
extern void __sha256_update(sha256_ctx *, const void *, size_t);
extern void __sha256_final(sha256_ctx *, uint8_t out[32]);

#define ROUNDS_DEFAULT 5000U
#define ROUNDS_MIN     1000U
#define ROUNDS_MAX     999999999U
#define SALT_MAX       16

static const char b64_alphabet[] =
    "./0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

/* Encode three bytes into four base64 chars, but with the bit
 * groups taken in Drepper's specific order (n bits, low-first).
 * `n` is 3, 2 or 1 (final group). */
static void
b64_from_24bit(char **out, char *out_end, uint32_t b2, uint32_t b1,
               uint32_t b0, int n)
{
    uint32_t w = (b2 << 16) | (b1 << 8) | b0;
    int      i;
    int      slots = (n == 3) ? 4 : (n == 2) ? 3 : 2;
    for (i = 0; i < slots && *out < out_end; i++) {
        **out = b64_alphabet[w & 0x3f];
        (*out)++;
        w >>= 6;
    }
}

char *
__crypt_sha256(const char *key, const char *setting, char *out, size_t outsz)
{
    sha256_ctx    ctxA, ctxB, ctxDP, ctxDS;
    uint8_t       altA[32], P[32], S[32], temp[32];
    const char   *salt;
    size_t        salt_len;
    size_t        key_len;
    uint32_t      rounds = ROUNDS_DEFAULT;
    int           rounds_custom = 0;
    size_t        i, cnt;
    char         *cp, *out_end;

    if (setting == NULL || strncmp(setting, "$5$", 3) != 0) {
        return NULL;
    }
    salt = setting + 3;

    if (strncmp(salt, "rounds=", 7) == 0) {
        const char *r = salt + 7;
        char       *eptr;
        unsigned long v = strtoul(r, &eptr, 10);
        if (*eptr == '$') {
            if (v < ROUNDS_MIN) v = ROUNDS_MIN;
            if (v > ROUNDS_MAX) v = ROUNDS_MAX;
            rounds = (uint32_t)v;
            rounds_custom = 1;
            salt = eptr + 1;
        }
    }

    /* Find end of salt (next '$' or end of string), cap to 16. */
    {
        const char *e = salt;
        while (*e != '\0' && *e != '$') e++;
        salt_len = (size_t)(e - salt);
        if (salt_len > SALT_MAX) salt_len = SALT_MAX;
    }
    key_len = strlen(key);

    /* Drepper's reference algorithm.  Steps are numbered in the spec. */
    __sha256_init(&ctxA);
    __sha256_update(&ctxA, key, key_len);     /* 2 */
    __sha256_update(&ctxA, salt, salt_len);   /* 3 */

    __sha256_init(&ctxB);                     /* 4 */
    __sha256_update(&ctxB, key, key_len);     /* 5 */
    __sha256_update(&ctxB, salt, salt_len);   /* 6 */
    __sha256_update(&ctxB, key, key_len);     /* 7 */
    __sha256_final(&ctxB, altA);              /* 8 */

    /* 9-10: feed altA into ctxA. */
    cnt = key_len;
    while (cnt > 32) {
        __sha256_update(&ctxA, altA, 32);
        cnt -= 32;
    }
    __sha256_update(&ctxA, altA, cnt);

    /* 11: for each bit of key_len from LSB upward — 0 → key, 1 → altA. */
    for (cnt = key_len; cnt > 0; cnt >>= 1) {
        if ((cnt & 1) != 0) {
            __sha256_update(&ctxA, altA, 32);
        } else {
            __sha256_update(&ctxA, key, key_len);
        }
    }
    __sha256_final(&ctxA, altA);              /* 12 */

    /* 13-15: build P. */
    __sha256_init(&ctxDP);
    for (cnt = 0; cnt < key_len; cnt++) {
        __sha256_update(&ctxDP, key, key_len);
    }
    __sha256_final(&ctxDP, temp);
    for (cnt = 0, cp = (char *)P; cnt + 32 <= key_len; cnt += 32, cp += 32) {
        memcpy(cp, temp, 32);
    }
    memcpy(cp, temp, key_len - cnt);

    /* 16-18: build S. */
    __sha256_init(&ctxDS);
    for (cnt = 0; cnt < (size_t)(16 + altA[0]); cnt++) {
        __sha256_update(&ctxDS, salt, salt_len);
    }
    __sha256_final(&ctxDS, temp);
    for (cnt = 0, cp = (char *)S; cnt + 32 <= salt_len; cnt += 32, cp += 32) {
        memcpy(cp, temp, 32);
    }
    memcpy(cp, temp, salt_len - cnt);

    /* 19-21: rounds. */
    for (i = 0; i < rounds; i++) {
        sha256_ctx c;
        __sha256_init(&c);
        if ((i & 1) != 0) {
            __sha256_update(&c, P, key_len);
        } else {
            __sha256_update(&c, altA, 32);
        }
        if (i % 3 != 0) {
            __sha256_update(&c, S, salt_len);
        }
        if (i % 7 != 0) {
            __sha256_update(&c, P, key_len);
        }
        if ((i & 1) != 0) {
            __sha256_update(&c, altA, 32);
        } else {
            __sha256_update(&c, P, key_len);
        }
        __sha256_final(&c, altA);
    }

    /* 22: emit output. */
    out_end = out + outsz;
    cp = out;
    if (rounds_custom) {
        int n = snprintf(out, outsz, "$5$rounds=%u$", rounds);
        if (n < 0 || (size_t)n >= outsz) return NULL;
        cp += n;
    } else {
        if (outsz < 4) return NULL;
        cp[0] = '$'; cp[1] = '5'; cp[2] = '$';
        cp += 3;
    }
    if ((size_t)(cp + salt_len + 1 - out) > outsz) return NULL;
    memcpy(cp, salt, salt_len);
    cp += salt_len;
    *cp++ = '$';

    /* Now write 43 base64 chars from altA[] in the wacky order. */
    b64_from_24bit(&cp, out_end, altA[0],  altA[10], altA[20], 3);
    b64_from_24bit(&cp, out_end, altA[21], altA[1],  altA[11], 3);
    b64_from_24bit(&cp, out_end, altA[12], altA[22], altA[2],  3);
    b64_from_24bit(&cp, out_end, altA[3],  altA[13], altA[23], 3);
    b64_from_24bit(&cp, out_end, altA[24], altA[4],  altA[14], 3);
    b64_from_24bit(&cp, out_end, altA[15], altA[25], altA[5],  3);
    b64_from_24bit(&cp, out_end, altA[6],  altA[16], altA[26], 3);
    b64_from_24bit(&cp, out_end, altA[27], altA[7],  altA[17], 3);
    b64_from_24bit(&cp, out_end, altA[18], altA[28], altA[8],  3);
    b64_from_24bit(&cp, out_end, altA[9],  altA[19], altA[29], 3);
    b64_from_24bit(&cp, out_end, 0,        altA[31], altA[30], 2);

    if (cp >= out_end) return NULL;
    *cp = '\0';

    /* Scrub. */
    memset(&ctxA, 0, sizeof(ctxA));
    memset(&ctxB, 0, sizeof(ctxB));
    memset(&ctxDP, 0, sizeof(ctxDP));
    memset(&ctxDS, 0, sizeof(ctxDS));
    memset(altA, 0, sizeof(altA));
    memset(P, 0, sizeof(P));
    memset(S, 0, sizeof(S));
    memset(temp, 0, sizeof(temp));

    return out;
}
