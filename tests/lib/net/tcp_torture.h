/*
 * tcp_torture.h — shared wire protocol + helpers for the two-machine
 * TCP torture test (torture_tcp_net client <-> tcp_partner server).
 *
 * No host addresses live here or in either .c file — the client takes
 * the partner's address as argv.  Payloads are a deterministic LCG
 * stream keyed by a per-scenario seed, so the receiver verifies every
 * byte against the expected sequence: any corruption, reordering, dup,
 * or truncation is caught without transmitting a separate checksum.
 */
#ifndef TCP_TORTURE_H
#define TCP_TORTURE_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>

#define TT_MAGIC   0x54435054u   /* "TCPT" */
#define TT_PORT    "5430"        /* default; override via argv */

enum {
    SC_ECHO      = 1,  /* client sends N, partner echoes, client verifies   */
    SC_DOWNLOAD  = 2,  /* partner sends N, client verifies                  */
    SC_UPLOAD    = 3,  /* client sends N, partner verifies, replies status  */
    SC_SLOWREAD  = 4,  /* like UPLOAD but partner drains slowly (backpress.) */
    SC_HALFCLOSE = 5,  /* client sends N then SHUT_WR, partner drains to EOF */
};

/* Request header, all fields network byte order. */
struct tt_req {
    uint32_t magic;
    uint32_t scenario;
    uint32_t length;    /* payload bytes (meaning per scenario)            */
    uint32_t seed;      /* LCG seed for the verifiable byte stream         */
    uint32_t flags;     /* SLOWREAD: ms to sleep per 4 KiB drained         */
};

/* Reply for UPLOAD/SLOWREAD/HALFCLOSE, all fields network byte order. */
struct tt_reply {
    uint32_t status;    /* 0 = all bytes matched; else (mismatch_off + 1)  */
    uint32_t received;  /* bytes the partner actually read                 */
};

/* Deterministic payload byte stream — a classic LCG.  Both ends seed
 * with the same value and produce the identical sequence. */
struct tt_rng { uint32_t s; };
static inline void tt_seed(struct tt_rng *r, uint32_t seed) { r->s = seed; }
static inline uint8_t tt_byte(struct tt_rng *r) {
    r->s = r->s * 1103515245u + 12345u;
    return (uint8_t)((r->s >> 16) & 0xFF);
}

/* read() exactly n bytes (EINTR-restart, EOF-aware).  Returns n, or the
 * short count on EOF, or -1 on error. */
static inline ssize_t tt_readn(int fd, void *buf, size_t n) {
    unsigned char *p = buf; size_t got = 0;
    while (got < n) {
        ssize_t r = read(fd, p + got, n - got);
        if (r > 0) { got += (size_t)r; continue; }
        if (r == 0) break;                       /* EOF */
        if (errno == EINTR) continue;
        return -1;
    }
    return (ssize_t)got;
}

/* write() exactly n bytes (EINTR-restart).  Returns n or -1. */
static inline ssize_t tt_writen(int fd, const void *buf, size_t n) {
    const unsigned char *p = buf; size_t put = 0;
    while (put < n) {
        ssize_t w = write(fd, p + put, n - put);
        if (w > 0) { put += (size_t)w; continue; }
        if (w < 0 && errno == EINTR) continue;
        return -1;
    }
    return (ssize_t)put;
}

/* Generate `n` LCG bytes from `seed` and write them; returns 0 / -1. */
static inline int tt_send_stream(int fd, uint32_t seed, uint32_t n) {
    struct tt_rng r; tt_seed(&r, seed);
    unsigned char chunk[4096];
    uint32_t left = n;
    while (left) {
        uint32_t c = left < sizeof(chunk) ? left : (uint32_t)sizeof(chunk);
        for (uint32_t i = 0; i < c; i++) chunk[i] = tt_byte(&r);
        if (tt_writen(fd, chunk, c) != (ssize_t)c) return -1;
        left -= c;
    }
    return 0;
}

/* Read `n` bytes and verify they match the LCG(seed) stream.  Returns
 * 0 if all matched, (offset+1) of the first mismatch, or -1 on I/O
 * error / short read.  *out_received gets the byte count actually read. */
static inline long tt_verify_stream(int fd, uint32_t seed, uint32_t n,
                                    uint32_t *out_received) {
    struct tt_rng r; tt_seed(&r, seed);
    unsigned char chunk[4096];
    uint32_t off = 0; long mism = 0;
    while (off < n) {
        uint32_t want = n - off < sizeof(chunk) ? n - off : (uint32_t)sizeof(chunk);
        ssize_t got = tt_readn(fd, chunk, want);
        if (got < 0) { if (out_received) *out_received = off; return -1; }
        for (ssize_t i = 0; i < got; i++) {
            if (chunk[i] != tt_byte(&r)) { mism = (long)(off + (uint32_t)i) + 1; break; }
        }
        off += (uint32_t)got;
        if (mism) break;
        if ((uint32_t)got < want) break;         /* short read = EOF */
    }
    if (out_received) *out_received = off;
    if (mism) return mism;
    if (off < n) return -1;                       /* truncated */
    return 0;
}

#endif /* TCP_TORTURE_H */
