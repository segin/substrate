/*
 * dn_expand.c + dn_skipname + ns_name_unpack — DNS name
 * decompression per RFC 1035 §4.1.4.
 *
 * Wire-form names are a sequence of length-prefixed labels, with
 * label-byte top-bits 11 meaning "this is a 2-byte pointer to
 * elsewhere in the message; follow it".  We cap recursion at 16
 * jumps to defeat malicious cycles.
 */

#include <arpa/nameser.h>
#include <resolv.h>
#include <string.h>

#define MAX_JUMPS 16

/* Unpack into wire form (length-prefixed labels, terminated by 0).
 * Returns the number of bytes consumed from `src` or -1. */
int ns_name_unpack(const unsigned char *msg, const unsigned char *eom,
                   const unsigned char *src, unsigned char *dst, size_t dstsiz) {
    const unsigned char *p = src;
    unsigned char *d = dst;
    int consumed = 0;        /* bytes consumed from original src */
    int jumps = 0;
    int seen_pointer = 0;

    if (!msg || !eom || !src || !dst || dstsiz == 0) return -1;

    while (p < eom) {
        unsigned char b = *p;
        if ((b & 0xC0) == 0xC0) {
            if (p + 1 >= eom) return -1;
            if (!seen_pointer) consumed += 2;
            seen_pointer = 1;
            if (++jumps > MAX_JUMPS) return -1;
            unsigned offset = ((unsigned)(b & 0x3F) << 8) | p[1];
            if (offset >= (unsigned)(eom - msg)) return -1;
            p = msg + offset;
            continue;
        }
        if ((b & 0xC0) != 0) return -1;   /* reserved */

        if (!seen_pointer) consumed += 1;
        if (b == 0) {
            if ((size_t)(d - dst) >= dstsiz) return -1;
            *d++ = 0;
            return seen_pointer ? consumed : (int)(p + 1 - src);
        }

        if (b > NS_MAXLABEL) return -1;
        if (p + 1 + b >= eom) return -1;
        if ((size_t)(d - dst) + 1 + b + 1 > dstsiz) return -1;
        *d++ = b;
        memcpy(d, p + 1, b);
        d += b;
        p += 1 + b;
        if (!seen_pointer) consumed += b;
    }
    return -1;
}

/* Convert a wire-form name (output of ns_name_unpack) to an ASCII
 * dotted name in `dst`.  Returns 0 on success, -1 on overflow. */
static int wire_to_ascii(const unsigned char *src, char *dst, size_t dstsiz) {
    size_t out = 0;
    while (*src) {
        unsigned char len = *src++;
        if (out + len + 1 + 1 > dstsiz) return -1;
        if (out > 0) dst[out++] = '.';
        for (int i = 0; i < len; i++) {
            unsigned char c = *src++;
            /* Escape control / dot bytes per BIND convention. */
            if (c == '.' || c == '\\') {
                if (out + 2 + 1 > dstsiz) return -1;
                dst[out++] = '\\';
                dst[out++] = (char)c;
            } else if (c < 0x21 || c > 0x7E) {
                if (out + 4 + 1 > dstsiz) return -1;
                dst[out++] = '\\';
                dst[out++] = '0' + (c / 100);
                dst[out++] = '0' + ((c / 10) % 10);
                dst[out++] = '0' + (c % 10);
            } else {
                dst[out++] = (char)c;
            }
        }
    }
    if (out == 0 && dstsiz > 1) dst[out++] = '.';   /* root */
    dst[out] = '\0';
    return 0;
}

int dn_expand(const unsigned char *msg, const unsigned char *eomorig,
              const unsigned char *src, char *dst, int dstsiz) {
    unsigned char wire[NS_MAXCDNAME];
    int n = ns_name_unpack(msg, eomorig, src, wire, sizeof(wire));
    if (n < 0) return -1;
    if (wire_to_ascii(wire, dst, (size_t)dstsiz) < 0) return -1;
    return n;
}

int dn_skipname(const unsigned char *src, const unsigned char *eom) {
    unsigned char wire[NS_MAXCDNAME];
    /* ns_name_unpack returns the consumed-from-src count even when
     * it follows pointers; that's exactly what skipname wants. */
    return ns_name_unpack(src, eom, src, wire, sizeof(wire));
}

int ns_name_uncompress(const unsigned char *msg, const unsigned char *eom,
                       const unsigned char *src, char *dst, size_t dstsiz) {
    return dn_expand(msg, eom, src, dst, (int)dstsiz);
}
