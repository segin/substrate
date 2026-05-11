/*
 * lib/c/src/crypt_des.c — traditional Unix DES crypt.
 *
 * 25 rounds of DES with 56 bits of key (first 8 chars of password
 * with the high bit stripped) and a 12-bit "salt" that permutes
 * the E-box.  Output: 2-char salt + 11-char base64 = 13 chars.
 *
 * KEEP FOR LEGACY COMPATIBILITY ONLY.  56-bit DES has been
 * brute-forceable in hours since the late 1990s.  Listed in
 * crypt.h with a big warning.
 *
 * This is a compact reference implementation in the public-domain
 * style of David Burren / ufc-crypt.  No SBox-precompute tricks —
 * the goal is correctness and readability, not speed.
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static const char b64_alphabet[] =
    "./0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

/*
 * DES tables — all 1-indexed per FIPS 46-3 but we apply -1 inline.
 */

/* PC-1: 64 -> 56 (parity bits dropped). */
static const uint8_t PC1[56] = {
    57,49,41,33,25,17, 9, 1,58,50,42,34,26,18,
    10, 2,59,51,43,35,27,19,11, 3,60,52,44,36,
    63,55,47,39,31,23,15, 7,62,54,46,38,30,22,
    14, 6,61,53,45,37,29,21,13, 5,28,20,12, 4
};

/* PC-2: 56 -> 48 (per-round subkey). */
static const uint8_t PC2[48] = {
    14,17,11,24, 1, 5, 3,28,15, 6,21,10,
    23,19,12, 4,26, 8,16, 7,27,20,13, 2,
    41,52,31,37,47,55,30,40,51,45,33,48,
    44,49,39,56,34,53,46,42,50,36,29,32
};

/* Initial permutation. */
static const uint8_t IP[64] = {
    58,50,42,34,26,18,10, 2,60,52,44,36,28,20,12, 4,
    62,54,46,38,30,22,14, 6,64,56,48,40,32,24,16, 8,
    57,49,41,33,25,17, 9, 1,59,51,43,35,27,19,11, 3,
    61,53,45,37,29,21,13, 5,63,55,47,39,31,23,15, 7
};

/* Final permutation = inverse of IP. */
static const uint8_t FP[64] = {
    40, 8,48,16,56,24,64,32,39, 7,47,15,55,23,63,31,
    38, 6,46,14,54,22,62,30,37, 5,45,13,53,21,61,29,
    36, 4,44,12,52,20,60,28,35, 3,43,11,51,19,59,27,
    34, 2,42,10,50,18,58,26,33, 1,41, 9,49,17,57,25
};

/* Expansion: 32 -> 48.  Crypt(3) "salts" by swapping bits between
 * E[i] and E[i+24]; we apply the salt by modifying this table in
 * place at runtime. */
static const uint8_t E_BASE[48] = {
    32, 1, 2, 3, 4, 5, 4, 5, 6, 7, 8, 9,
     8, 9,10,11,12,13,12,13,14,15,16,17,
    16,17,18,19,20,21,20,21,22,23,24,25,
    24,25,26,27,28,29,28,29,30,31,32, 1
};

/* P-box. */
static const uint8_t P[32] = {
    16, 7,20,21,29,12,28,17, 1,15,23,26, 5,18,31,10,
     2, 8,24,14,32,27, 3, 9,19,13,30, 6,22,11, 4,25
};

/* S-boxes. */
static const uint8_t S[8][64] = {
    {
        14, 4,13, 1, 2,15,11, 8, 3,10, 6,12, 5, 9, 0, 7,
         0,15, 7, 4,14, 2,13, 1,10, 6,12,11, 9, 5, 3, 8,
         4, 1,14, 8,13, 6, 2,11,15,12, 9, 7, 3,10, 5, 0,
        15,12, 8, 2, 4, 9, 1, 7, 5,11, 3,14,10, 0, 6,13
    },{
        15, 1, 8,14, 6,11, 3, 4, 9, 7, 2,13,12, 0, 5,10,
         3,13, 4, 7,15, 2, 8,14,12, 0, 1,10, 6, 9,11, 5,
         0,14, 7,11,10, 4,13, 1, 5, 8,12, 6, 9, 3, 2,15,
        13, 8,10, 1, 3,15, 4, 2,11, 6, 7,12, 0, 5,14, 9
    },{
        10, 0, 9,14, 6, 3,15, 5, 1,13,12, 7,11, 4, 2, 8,
        13, 7, 0, 9, 3, 4, 6,10, 2, 8, 5,14,12,11,15, 1,
        13, 6, 4, 9, 8,15, 3, 0,11, 1, 2,12, 5,10,14, 7,
         1,10,13, 0, 6, 9, 8, 7, 4,15,14, 3,11, 5, 2,12
    },{
         7,13,14, 3, 0, 6, 9,10, 1, 2, 8, 5,11,12, 4,15,
        13, 8,11, 5, 6,15, 0, 3, 4, 7, 2,12, 1,10,14, 9,
        10, 6, 9, 0,12,11, 7,13,15, 1, 3,14, 5, 2, 8, 4,
         3,15, 0, 6,10, 1,13, 8, 9, 4, 5,11,12, 7, 2,14
    },{
         2,12, 4, 1, 7,10,11, 6, 8, 5, 3,15,13, 0,14, 9,
        14,11, 2,12, 4, 7,13, 1, 5, 0,15,10, 3, 9, 8, 6,
         4, 2, 1,11,10,13, 7, 8,15, 9,12, 5, 6, 3, 0,14,
        11, 8,12, 7, 1,14, 2,13, 6,15, 0, 9,10, 4, 5, 3
    },{
        12, 1,10,15, 9, 2, 6, 8, 0,13, 3, 4,14, 7, 5,11,
        10,15, 4, 2, 7,12, 9, 5, 6, 1,13,14, 0,11, 3, 8,
         9,14,15, 5, 2, 8,12, 3, 7, 0, 4,10, 1,13,11, 6,
         4, 3, 2,12, 9, 5,15,10,11,14, 1, 7, 6, 0, 8,13
    },{
         4,11, 2,14,15, 0, 8,13, 3,12, 9, 7, 5,10, 6, 1,
        13, 0,11, 7, 4, 9, 1,10,14, 3, 5,12, 2,15, 8, 6,
         1, 4,11,13,12, 3, 7,14,10,15, 6, 8, 0, 5, 9, 2,
         6,11,13, 8, 1, 4,10, 7, 9, 5, 0,15,14, 2, 3,12
    },{
        13, 2, 8, 4, 6,15,11, 1,10, 9, 3,14, 5, 0,12, 7,
         1,15,13, 8,10, 3, 7, 4,12, 5, 6,11, 0,14, 9, 2,
         7,11, 4, 1, 9,12,14, 2, 0, 6,10,13,15, 3, 5, 8,
         2, 1,14, 7, 4,10, 8,13,15,12, 9, 0, 3, 5, 6,11
    }
};

/* Per-round shift schedule. */
static const uint8_t SHIFT[16] = {
    1,1,2,2,2,2,2,2,1,2,2,2,2,2,2,1
};

/* --- helpers: bit manipulation in big-endian bit arrays. --- */
static int
get_bit(const uint8_t *src, int bit)
{
    return (src[bit >> 3] >> (7 - (bit & 7))) & 1;
}

static void
set_bit(uint8_t *dst, int bit, int v)
{
    if (v) dst[bit >> 3] |=  (1 << (7 - (bit & 7)));
    else   dst[bit >> 3] &= ~(1 << (7 - (bit & 7)));
}

static void
permute(uint8_t *dst, const uint8_t *src, const uint8_t *table, int n)
{
    int i;
    memset(dst, 0, (n + 7) / 8);
    for (i = 0; i < n; i++) {
        set_bit(dst, i, get_bit(src, table[i] - 1));
    }
}

static void
des_setkey(const uint8_t key8[8], uint8_t subkeys[16][6])
{
    uint8_t key56[7];
    int     i;

    permute(key56, key8, PC1, 56);

    /* Split key56 into two 28-bit halves.  We pack each half left-
     * aligned in 32 bits via a small helper. */
    {
        uint32_t kc = 0, kd = 0;
        for (i = 0; i < 28; i++) kc = (kc << 1) | get_bit(key56, i);
        for (i = 0; i < 28; i++) kd = (kd << 1) | get_bit(key56, 28 + i);

        for (i = 0; i < 16; i++) {
            int   shift = SHIFT[i];
            uint8_t cd[7];
            uint32_t maskc = kc, maskd = kd;
            /* Rotate left by `shift` within 28 bits. */
            maskc = ((maskc << shift) | (maskc >> (28 - shift))) & 0x0fffffff;
            maskd = ((maskd << shift) | (maskd >> (28 - shift))) & 0x0fffffff;
            kc = maskc;
            kd = maskd;
            /* Repack into cd (56 bits). */
            memset(cd, 0, sizeof(cd));
            {
                int j;
                for (j = 0; j < 28; j++)
                    set_bit(cd, j,
                            (int)((maskc >> (27 - j)) & 1));
                for (j = 0; j < 28; j++)
                    set_bit(cd, 28 + j,
                            (int)((maskd >> (27 - j)) & 1));
            }
            permute(subkeys[i], cd, PC2, 48);
        }
    }
}

static void
des_round(uint8_t L[4], uint8_t R[4], const uint8_t subkey[6],
          const uint8_t E_eff[48])
{
    uint8_t exp48[6];
    uint8_t sbox_in[6];
    uint8_t sbox_out[4];
    uint8_t pbox_out[4];
    int     i;

    permute(exp48, R, E_eff, 48);

    /* XOR with subkey. */
    for (i = 0; i < 6; i++) sbox_in[i] = exp48[i] ^ subkey[i];

    /* 8 S-box lookups: each takes 6 bits, returns 4. */
    memset(sbox_out, 0, sizeof(sbox_out));
    for (i = 0; i < 8; i++) {
        int b0 = get_bit(sbox_in, i*6 + 0);
        int b1 = get_bit(sbox_in, i*6 + 1);
        int b2 = get_bit(sbox_in, i*6 + 2);
        int b3 = get_bit(sbox_in, i*6 + 3);
        int b4 = get_bit(sbox_in, i*6 + 4);
        int b5 = get_bit(sbox_in, i*6 + 5);
        int row = (b0 << 1) | b5;
        int col = (b1 << 3) | (b2 << 2) | (b3 << 1) | b4;
        int v = S[i][row * 16 + col];
        set_bit(sbox_out, i*4 + 0, (v >> 3) & 1);
        set_bit(sbox_out, i*4 + 1, (v >> 2) & 1);
        set_bit(sbox_out, i*4 + 2, (v >> 1) & 1);
        set_bit(sbox_out, i*4 + 3, (v >> 0) & 1);
    }

    permute(pbox_out, sbox_out, P, 32);

    /* L, R = R, L ^ pbox_out. */
    {
        uint8_t newR[4];
        for (i = 0; i < 4; i++) newR[i] = L[i] ^ pbox_out[i];
        memcpy(L, R, 4);
        memcpy(R, newR, 4);
    }
}

/*
 * Pack a 13-bit (or smaller) salt into a 48-bit expansion-table
 * permutation.  Each of the 12 salt bits (b0..b11) controls a pair
 * of E entries: if bit i is set, swap E[i] with E[i+24].
 */
static int
salt_to_int(const char *salt)
{
    int  v = 0;
    int  i;
    for (i = 1; i >= 0; i--) {
        char c = salt[i];
        int  d;
        if      (c >= 'a' && c <= 'z') d = c - 'a' + 38;
        else if (c >= 'A' && c <= 'Z') d = c - 'A' + 12;
        else if (c >= '0' && c <= '9') d = c - '0' + 2;
        else if (c == '.') d = 0;
        else if (c == '/') d = 1;
        else return -1;
        v = (v << 6) | d;
    }
    return v;
}

static void
apply_salt_to_E(uint8_t E_eff[48], int salt_val)
{
    int i;
    memcpy(E_eff, E_BASE, 48);
    for (i = 0; i < 12; i++) {
        if (salt_val & (1 << i)) {
            uint8_t t = E_eff[i];
            E_eff[i] = E_eff[i + 24];
            E_eff[i + 24] = t;
        }
    }
}

static void
des_encrypt_block(uint8_t out[8], const uint8_t in[8],
                  uint8_t subkeys[16][6], const uint8_t E_eff[48])
{
    uint8_t buf[8];
    uint8_t L[4], R[4];
    int     i;

    permute(buf, in, IP, 64);
    memcpy(L, buf, 4);
    memcpy(R, buf + 4, 4);
    for (i = 0; i < 16; i++) {
        des_round(L, R, subkeys[i], E_eff);
    }
    /* Final swap before FP: R || L. */
    memcpy(buf, R, 4);
    memcpy(buf + 4, L, 4);
    permute(out, buf, FP, 64);
}

char *
__crypt_des(const char *key, const char *setting, char *out, size_t outsz)
{
    uint8_t k8[8] = {0};
    uint8_t subkeys[16][6];
    uint8_t E_eff[48];
    uint8_t block[8] = {0};
    int     salt_val;
    int     i;
    char   *cp;

    if (setting == NULL || outsz < 14) return NULL;
    if (setting[0] == '\0' || setting[1] == '\0') return NULL;

    salt_val = salt_to_int(setting);
    if (salt_val < 0) return NULL;

    /* Pack first 8 chars of `key` into k8 with the LSB always 0
     * (DES parity bit, ignored).  The traditional crypt(3) shifts
     * each char left by one. */
    for (i = 0; i < 8 && key[i] != '\0'; i++) {
        k8[i] = (uint8_t)(key[i] << 1);
    }

    des_setkey(k8, subkeys);
    apply_salt_to_E(E_eff, salt_val);

    /* 25 rounds of "encrypt 64 zero bits with this key". */
    for (i = 0; i < 25; i++) {
        des_encrypt_block(block, block, subkeys, E_eff);
    }

    cp = out;
    *cp++ = setting[0];
    *cp++ = setting[1];

    /* Pack 64 output bits into 11 base64 chars (66 bits with low 2
     * pad-zero — traditional crypt format). */
    {
        int bit = 0;
        for (i = 0; i < 11; i++) {
            int v = 0;
            int j;
            for (j = 0; j < 6; j++) {
                if (bit < 64) v |= get_bit(block, bit) << (5 - j);
                bit++;
            }
            *cp++ = b64_alphabet[v];
        }
    }
    *cp = '\0';

    memset(k8, 0, sizeof(k8));
    memset(subkeys, 0, sizeof(subkeys));
    memset(block, 0, sizeof(block));
    return out;
}
