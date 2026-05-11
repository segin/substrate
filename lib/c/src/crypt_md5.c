/*
 * lib/c/src/crypt_md5.c — MD5 crypt ($1$).
 *
 * Implements Poul-Henning Kamp's MD5 crypt (FreeBSD ~1994).  Output
 * format:
 *
 *     $1$<salt>$<22-char-base64>
 *
 * Salt up to 8 chars.  Fixed 1000 rounds.  KEEP FOR COMPATIBILITY
 * ONLY — MD5 has been collision-broken for two decades and this
 * scheme is far too fast for modern brute force.  Listed in
 * crypt.h with a big warning.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint32_t h[4];
    uint64_t length;
    uint8_t  buf[64];
    size_t   buflen;
} md5_ctx;
extern void __md5_init(md5_ctx *);
extern void __md5_update(md5_ctx *, const void *, size_t);
extern void __md5_final(md5_ctx *, uint8_t out[16]);

#define SALT_MAX 8

static const char b64_alphabet[] =
    "./0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

static void
b64_from_24bit(char **out, char *out_end, uint32_t b2, uint32_t b1,
               uint32_t b0, int slots)
{
    uint32_t w = (b2 << 16) | (b1 << 8) | b0;
    int      i;
    for (i = 0; i < slots && *out < out_end; i++) {
        **out = b64_alphabet[w & 0x3f];
        (*out)++;
        w >>= 6;
    }
}

char *
__crypt_md5(const char *key, const char *setting, char *out, size_t outsz)
{
    const char *salt;
    size_t      salt_len;
    size_t      key_len;
    md5_ctx     ctx, ctx1;
    uint8_t     digest[16];
    int         i;
    size_t      cnt;
    char       *cp, *out_end;

    if (setting == NULL || strncmp(setting, "$1$", 3) != 0) {
        return NULL;
    }
    salt = setting + 3;
    {
        const char *e = salt;
        while (*e != '\0' && *e != '$') e++;
        salt_len = (size_t)(e - salt);
        if (salt_len > SALT_MAX) salt_len = SALT_MAX;
    }
    key_len = strlen(key);

    /* Step 1-3: H = MD5(key + "$1$" + salt + MD5(key + salt + key)). */
    __md5_init(&ctx1);
    __md5_update(&ctx1, key, key_len);
    __md5_update(&ctx1, salt, salt_len);
    __md5_update(&ctx1, key, key_len);
    __md5_final(&ctx1, digest);

    __md5_init(&ctx);
    __md5_update(&ctx, key, key_len);
    __md5_update(&ctx, "$1$", 3);
    __md5_update(&ctx, salt, salt_len);
    for (cnt = key_len; cnt > 16; cnt -= 16) {
        __md5_update(&ctx, digest, 16);
    }
    __md5_update(&ctx, digest, cnt);

    /*
     * "For each bit of the password length", LSB upward: when bit
     * is set, feed a zero byte; when clear, feed the password's
     * first byte.  PHK's original FreeBSD reference.
     */
    for (cnt = key_len; cnt != 0; cnt >>= 1) {
        if (cnt & 1) {
            __md5_update(&ctx, "\0", 1);
        } else {
            __md5_update(&ctx, key, 1);
        }
    }
    __md5_final(&ctx, digest);

    /* 1000 rounds. */
    for (i = 0; i < 1000; i++) {
        __md5_init(&ctx);
        if (i & 1) {
            __md5_update(&ctx, key, key_len);
        } else {
            __md5_update(&ctx, digest, 16);
        }
        if (i % 3) {
            __md5_update(&ctx, salt, salt_len);
        }
        if (i % 7) {
            __md5_update(&ctx, key, key_len);
        }
        if (i & 1) {
            __md5_update(&ctx, digest, 16);
        } else {
            __md5_update(&ctx, key, key_len);
        }
        __md5_final(&ctx, digest);
    }

    out_end = out + outsz;
    cp = out;
    if (outsz < 4 + salt_len + 1 + 22 + 1) return NULL;
    cp[0] = '$'; cp[1] = '1'; cp[2] = '$';
    cp += 3;
    memcpy(cp, salt, salt_len);
    cp += salt_len;
    *cp++ = '$';

    b64_from_24bit(&cp, out_end, digest[0], digest[6],  digest[12], 4);
    b64_from_24bit(&cp, out_end, digest[1], digest[7],  digest[13], 4);
    b64_from_24bit(&cp, out_end, digest[2], digest[8],  digest[14], 4);
    b64_from_24bit(&cp, out_end, digest[3], digest[9],  digest[15], 4);
    b64_from_24bit(&cp, out_end, digest[4], digest[10], digest[5],  4);
    b64_from_24bit(&cp, out_end, 0,         0,          digest[11], 2);

    if (cp >= out_end) return NULL;
    *cp = '\0';

    memset(&ctx, 0, sizeof(ctx));
    memset(&ctx1, 0, sizeof(ctx1));
    memset(digest, 0, sizeof(digest));
    return out;
}
