/*
 * res_mkquery.c — build a DNS query packet.
 *
 * Format: 12-byte header followed by one question (name, type,
 * class).  No additional records.  ID is taken from _res.id and
 * incremented for the next call.
 */

#include <arpa/nameser.h>
#include <netinet/in.h>
#include <resolv.h>
#include <string.h>

/* Encode an ASCII dotted name into wire format.  Returns the number
 * of bytes written (including the terminating 0) or -1 on overflow /
 * label-too-long. */
static int encode_name(const char *src, unsigned char *dst, int dstlen) {
    if (!src || !dst) return -1;
    const char *p = src;
    int out = 0;
    while (*p) {
        const char *seg = p;
        int seglen = 0;
        while (*p && *p != '.') { p++; seglen++; }
        if (seglen == 0 || seglen > NS_MAXLABEL) return -1;
        if (out + 1 + seglen + 1 > dstlen) return -1;
        dst[out++] = (unsigned char)seglen;
        memcpy(dst + out, seg, seglen);
        out += seglen;
        if (*p == '.') p++;
    }
    if (out + 1 > dstlen) return -1;
    dst[out++] = 0;
    return out;
}

int res_mkquery(int op, const char *dname, int rrclass, int type,
                const unsigned char *data, int datalen,
                const unsigned char *newrr, unsigned char *buf, int buflen) {
    (void)data;
    (void)datalen;
    (void)newrr;

    if (!dname || !buf || buflen < NS_HFIXEDSZ + 5) return -1;
    if ((_res.options & RES_INIT) == 0 && res_init() < 0) return -1;

    /* Header. */
    memset(buf, 0, NS_HFIXEDSZ);
    uint16_t id = ++_res.id;
    buf[0] = (unsigned char)(id >> 8);
    buf[1] = (unsigned char)id;
    /* flags: opcode<<11, RD=1.  Substrate always sets RD; recursive
     * resolution is the only mode our nameserver is going to do. */
    uint16_t flags = ((uint16_t)(op & 0xF) << 11);
    if (_res.options & RES_RECURSE) flags |= 0x0100; /* RD */
    buf[2] = (unsigned char)(flags >> 8);
    buf[3] = (unsigned char)flags;
    /* qdcount = 1, others = 0. */
    buf[4] = 0; buf[5] = 1;

    /* Question section. */
    int qlen = encode_name(dname, buf + NS_HFIXEDSZ, buflen - NS_HFIXEDSZ - 4);
    if (qlen < 0) return -1;
    int off = NS_HFIXEDSZ + qlen;
    if (off + 4 > buflen) return -1;
    /* qtype */
    buf[off++] = (unsigned char)(type >> 8);
    buf[off++] = (unsigned char)type;
    /* qclass */
    buf[off++] = (unsigned char)(rrclass >> 8);
    buf[off++] = (unsigned char)rrclass;
    return off;
}
