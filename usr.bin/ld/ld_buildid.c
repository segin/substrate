/*
 * ld_buildid.c -- --build-id: the .note.gnu.build-id note, as GNU ld
 * makes it.
 *
 * The note (NT_GNU_BUILD_ID, owner "GNU") is made as an input section of
 * an object of the linker's own, so that a script places it as it places
 * any note (the vDSO's `.note : { *(.note*) }`) and the default layout
 * puts it among the read-only sections before the code.  Once the output
 * is written its descriptor is filled in: for sha1 and md5 a digest of the
 * whole file, the descriptor zero while it is taken, so that the same
 * output has the same id; for uuid 16 random bytes; for 0xHEX those bytes.
 */

#include "ld.h"

#include <fcntl.h>

/* ---- SHA-1 (FIPS 180-1) and MD5 (RFC 1321), enough for a file's digest ----------------------------- */

typedef struct {
    uint32_t h[5];
    uint64_t len;
    uint8_t buf[64];
    size_t n;
} sha1_t;

static uint32_t rol32(uint32_t x, int s) {
    return (x << s) | (x >> (32 - s));
}

static void sha1_block(sha1_t *c, const uint8_t *p) {
    uint32_t w[80];
    uint32_t a, b, d, e, f, k, t, cc;
    int i;

    for (i = 0; i < 16; ++i) {
        w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 | (uint32_t)p[4 * i + 2] << 8 | p[4 * i + 3];
    }
    for (i = 16; i < 80; ++i) {
        w[i] = rol32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    }
    a = c->h[0];
    b = c->h[1];
    cc = c->h[2];
    d = c->h[3];
    e = c->h[4];
    for (i = 0; i < 80; ++i) {
        if (i < 20) {
            f = (b & cc) | (~b & d);
            k = 0x5a827999u;
        } else if (i < 40) {
            f = b ^ cc ^ d;
            k = 0x6ed9eba1u;
        } else if (i < 60) {
            f = (b & cc) | (b & d) | (cc & d);
            k = 0x8f1bbcdcu;
        } else {
            f = b ^ cc ^ d;
            k = 0xca62c1d6u;
        }
        t = rol32(a, 5) + f + e + k + w[i];
        e = d;
        d = cc;
        cc = rol32(b, 30);
        b = a;
        a = t;
    }
    c->h[0] += a;
    c->h[1] += b;
    c->h[2] += cc;
    c->h[3] += d;
    c->h[4] += e;
}

static void sha1_init(sha1_t *c) {
    static const uint32_t iv[5] = { 0x67452301u, 0xefcdab89u, 0x98badcfeu, 0x10325476u, 0xc3d2e1f0u };

    memset(c, 0, sizeof(*c));
    memcpy(c->h, iv, sizeof(iv));
}

static void sha1_update(sha1_t *c, const uint8_t *p, size_t n) {
    c->len += n;
    while (n > 0) {
        size_t take = 64 - c->n < n ? 64 - c->n : n;
        memcpy(c->buf + c->n, p, take);
        c->n += take;
        p += take;
        n -= take;
        if (c->n == 64) {
            sha1_block(c, c->buf);
            c->n = 0;
        }
    }
}

static void sha1_final(sha1_t *c, uint8_t out[20]) {
    uint64_t bits = c->len * 8;
    uint8_t pad = 0x80;
    uint8_t lenb[8];
    int i;

    sha1_update(c, &pad, 1);
    pad = 0;
    while (c->n != 56) {
        sha1_update(c, &pad, 1);
    }
    for (i = 0; i < 8; ++i) {
        lenb[i] = (uint8_t)(bits >> (56 - 8 * i));
    }
    sha1_update(c, lenb, 8);
    for (i = 0; i < 20; ++i) {
        out[i] = (uint8_t)(c->h[i / 4] >> (24 - 8 * (i % 4)));
    }
}

static void md5_digest(const uint8_t *msg, size_t len, uint8_t out[16]) {
    static const uint32_t K[64] = {
        0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
        0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
        0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
        0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
        0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
        0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
        0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
        0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391 };
    static const int S[64] = { 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
                               5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20,
                               4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
                               6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21 };
    uint32_t h[4] = { 0x67452301u, 0xefcdab89u, 0x98badcfeu, 0x10325476u };
    uint64_t bits = (uint64_t)len * 8;
    size_t total = ((len + 8) / 64 + 1) * 64;
    size_t off, i;

    for (off = 0; off < total; off += 64) {
        uint32_t m[16];
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3];

        for (i = 0; i < 64; ++i) {
            size_t at = off + i;
            uint8_t byte = at < len ? msg[at] : at == len ? 0x80 : at >= total - 8 ? (uint8_t)(bits >> (8 * (at - (total - 8)))) : 0;
            if (i % 4 == 0) {
                m[i / 4] = 0;
            }
            m[i / 4] |= (uint32_t)byte << (8 * (i % 4));
        }
        for (i = 0; i < 64; ++i) {
            uint32_t f;
            size_t g;
            if (i < 16) {
                f = (b & c) | (~b & d);
                g = i;
            } else if (i < 32) {
                f = (d & b) | (~d & c);
                g = (5 * i + 1) % 16;
            } else if (i < 48) {
                f = b ^ c ^ d;
                g = (3 * i + 5) % 16;
            } else {
                f = c ^ (b | ~d);
                g = (7 * i) % 16;
            }
            f = f + a + K[i] + m[g];
            a = d;
            d = c;
            c = b;
            b = b + rol32(f, S[i]);
        }
        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
    }
    for (i = 0; i < 16; ++i) {
        out[i] = (uint8_t)(h[i / 4] >> (8 * (i % 4)));
    }
}

/* ---- the option and the note ------------------------------------------------------------------------ */

int ld_build_id_option(ld_options_t *opt, const char *val) {
    if (val == NULL || strcmp(val, "sha1") == 0 || strcmp(val, "tree") == 0) {
        opt->build_id_style = LD_BUILD_ID_SHA1;
        opt->build_id_size = 20;
    } else if (strcmp(val, "md5") == 0) {
        opt->build_id_style = LD_BUILD_ID_MD5;
        opt->build_id_size = 16;
    } else if (strcmp(val, "uuid") == 0) {
        opt->build_id_style = LD_BUILD_ID_UUID;
        opt->build_id_size = 16;
    } else if (strcmp(val, "none") == 0) {
        opt->build_id_style = LD_BUILD_ID_NONE;
        opt->build_id_size = 0;
    } else if ((val[0] == '0' && (val[1] == 'x' || val[1] == 'X'))) {
        const char *p = val + 2;
        size_t n = 0;

        /* hex digits, two to a byte, '-' and ':' between them read over (as GNU ld's) */
        while (*p != '\0') {
            int hi, lo;
            if (*p == '-' || *p == ':') {
                p++;
                continue;
            }
            hi = isxdigit((unsigned char)p[0]) ? (isdigit((unsigned char)p[0]) ? p[0] - '0' : (tolower((unsigned char)p[0]) - 'a' + 10)) : -1;
            lo = p[1] != '\0' && isxdigit((unsigned char)p[1])
                     ? (isdigit((unsigned char)p[1]) ? p[1] - '0' : (tolower((unsigned char)p[1]) - 'a' + 10))
                     : -1;
            if (hi < 0 || lo < 0 || n >= sizeof(opt->build_id_hex)) {
                fprintf(stderr, "ld: --build-id=%s: not an even number of hex digits\n", val);
                return LD_OPT_USAGE;
            }
            opt->build_id_hex[n++] = (uint8_t)(hi << 4 | lo);
            p += 2;
        }
        if (n == 0) {
            fprintf(stderr, "ld: --build-id=%s: no bytes\n", val);
            return LD_OPT_USAGE;
        }
        opt->build_id_style = LD_BUILD_ID_HEX;
        opt->build_id_size = n;
    } else {
        fprintf(stderr, "ld: invalid --build-id style '%s' (sha1, md5, uuid, 0xHEX, none)\n", val);
        return LD_OPT_USAGE;
    }
    return 0;
}

int add_build_id_input(ld_ctx_t *ctx, objvec_t *inputs) {
    const ld_options_t *opt = &ctx->opt;
    uint8_t note[16 + sizeof(opt->build_id_hex)];
    elfobj_t *first;
    elfobj_t *obj;
    elf_section_t *sec;

    if (opt->build_id_style == LD_BUILD_ID_NONE || inputs->count == 0) {
        return 0;
    }
    first = inputs->objs[0];
    obj = elf_create(ET_REL, elf_machine(first), elf_class(first), elf_endian(first));
    if (obj == NULL) {
        return -1;
    }
    /* namesz 4, descsz, NT_GNU_BUILD_ID (3), "GNU\0", the descriptor, zero for now */
    memset(note, 0, sizeof(note));
    write_u32_endian(note + 0, elf_endian(first), 4);
    write_u32_endian(note + 4, elf_endian(first), (uint32_t)opt->build_id_size);
    write_u32_endian(note + 8, elf_endian(first), 3);
    memcpy(note + 12, "GNU", 4);
    sec = elf_add_section(obj, ".note.gnu.build-id", SHT_NOTE, SHF_ALLOC);
    if (sec == NULL || elf_section_set_align(sec, 4) != ELF_OK ||
        elf_section_set_data(sec, note, 16 + opt->build_id_size) != ELF_OK ||
        objvec_push(inputs, obj, LD_BUILD_ID_INPUT) != 0) {
        elf_close(obj);
        return -1;
    }
    return 0;
}

int write_build_id(const ld_ctx_t *ctx, const char *path) {
    const ld_options_t *opt = &ctx->opt;
    uint8_t want[16];
    uint8_t *img = NULL;
    size_t len = 0, cap = 0, at, n = opt->build_id_size;
    uint8_t id[sizeof(opt->build_id_hex)];
    FILE *f;
    int c;
    int found = 0;

    if (opt->build_id_style == LD_BUILD_ID_NONE) {
        return 0;
    }
    f = fopen(path, "r+b");
    if (f == NULL) {
        return 0;   /* (not a file: a device, a pipe -- nothing to fill in) */
    }
    while ((c = fgetc(f)) != EOF) {
        if (len == cap) {
            uint8_t *next = (uint8_t *)realloc(img, cap == 0 ? 65536 : cap * 2);
            if (next == NULL) {
                free(img);
                fclose(f);
                return -1;
            }
            img = next;
            cap = cap == 0 ? 65536 : cap * 2;
        }
        img[len++] = (uint8_t)c;
    }
    /* the note: its header, "GNU", a descriptor of zeros */
    write_u32_endian(want + 0, img != NULL && len > 5 && img[5] == 2 ? ELFOBJ_ENDIAN_BE : ELFOBJ_ENDIAN_LE, 4);
    write_u32_endian(want + 4, img != NULL && len > 5 && img[5] == 2 ? ELFOBJ_ENDIAN_BE : ELFOBJ_ENDIAN_LE,
                     (uint32_t)n);
    write_u32_endian(want + 8, img != NULL && len > 5 && img[5] == 2 ? ELFOBJ_ENDIAN_BE : ELFOBJ_ENDIAN_LE, 3);
    memcpy(want + 12, "GNU", 4);
    for (at = 0; img != NULL && at + 16 + n <= len; at += 4) {
        size_t k;

        if (memcmp(img + at, want, 16) != 0) {
            continue;
        }
        for (k = 0; k < n && img[at + 16 + k] == 0; ++k) {
        }
        if (k == n) {
            found = 1;
            break;
        }
    }
    if (!found) {
        free(img);
        fclose(f);
        return 0;   /* (a script left it out: /DISCARD/) */
    }
    if (opt->build_id_style == LD_BUILD_ID_SHA1) {
        sha1_t s;
        sha1_init(&s);
        sha1_update(&s, img, len);
        sha1_final(&s, id);
    } else if (opt->build_id_style == LD_BUILD_ID_MD5) {
        md5_digest(img, len, id);
    } else if (opt->build_id_style == LD_BUILD_ID_UUID) {
        int fd = open("/dev/urandom", O_RDONLY);
        if (fd < 0 || read(fd, id, 16) != 16) {
            if (fd >= 0) {
                close(fd);
            }
            free(img);
            fclose(f);
            fprintf(stderr, "ld: --build-id=uuid: no random bytes\n");
            return -1;
        }
        close(fd);
    } else {
        memcpy(id, opt->build_id_hex, n);
    }
    free(img);
    if (fseek(f, (long)(at + 16), SEEK_SET) != 0 || fwrite(id, 1, n, f) != n) {
        fclose(f);
        return -1;
    }
    return fclose(f) == 0 ? 0 : -1;
}
