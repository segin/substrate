/*
 * torture_heap_stdio.c — combined libc heap + stdio stress.
 *
 * Motivation: two recent crashes on substrate both fault inside libc
 * userland with NULL/junk destinations:
 *
 *   1. Xfbdev XkmInsureSize -> calloc -> malloc -> split_block:
 *      lib/c/src/stdlib.c:214 dereferences a 0-or-tiny pointer
 *      computed from `(char*)block + BLOCK_META_SIZE + size`.
 *
 *   2. Xfbdev RegisterExtensionNames -> fgets -> fgetc -> fread ->
 *      memcpy: lib/c/src/string.c:113 writes to dst = 0x00000001.
 *
 *   3. links browser malloc -> split_block: same as (1).
 *
 * Two failure modes, one observation: the libc heap and stdio
 * buffers interact in ways that produce bogus pointers.  This test
 * runs both at once — alternating malloc/calloc/realloc/free with
 * fopen/fread/fgets/fclose against tmp files of varying sizes —
 * to drive whichever bug surfaces first.
 *
 * Each scenario reports its op count and any canary mismatch.  Final
 * "Result:" line is parsed by run-auto-test.sh.
 *
 * Substrate cross-build:
 *     make -f Makefile.sockets torture_heap_stdio \
 *          CROSS=/opt/substrate/bin/i386-unknown-substrate-
 * Host baseline:
 *     make -f Makefile.sockets torture_heap_stdio
 */

#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdint.h>
#include <fcntl.h>
#include <signal.h>
#include <setjmp.h>
#include <time.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/time.h>

/* ------------------------------------------------------------------ */
/* Common harness                                                      */
/* ------------------------------------------------------------------ */

static uint32_t g_rng = 0xC0FFEE13u;
static uint32_t rng_next(void) {
    uint32_t x = g_rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return g_rng = x;
}
static uint32_t rng_range(uint32_t lo, uint32_t hi) {
    return lo + rng_next() % (hi - lo + 1);
}

/* Canary derived from (ptr, size).  If a neighbour scribble lands on
 * us, the canary will mismatch on verify. */
static uint8_t canary_byte(void *p, size_t off) {
    uintptr_t x = (uintptr_t)p ^ (uintptr_t)off ^ 0xA5;
    return (uint8_t)((x * 2654435761u) >> 24);
}
static void canary_fill(void *p, size_t sz) {
    uint8_t *b = p;
    for (size_t i = 0; i < sz; i++) b[i] = canary_byte(p, i);
}
static int canary_check(void *p, size_t sz) {
    const uint8_t *b = p;
    for (size_t i = 0; i < sz; i++) {
        if (b[i] != canary_byte(p, i)) {
            fprintf(stderr, "  canary mismatch at p=%p off=%zu got=%02x want=%02x\n",
                    p, i, b[i], canary_byte(p, i));
            return 0;
        }
    }
    return 1;
}

#define FAIL(fmt, ...) do { \
    fprintf(stderr, "  FAIL: " fmt "\n", ##__VA_ARGS__); \
    return 1; \
} while (0)

/* Write a deterministic test file with `n` bytes.  Content: ascii
 * 'A'..'Z' cycling, plus a newline every `line_len` bytes.  Used by
 * stdio scenarios that need a stable input. */
static int make_test_file(const char *path, size_t n, size_t line_len) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    for (size_t i = 0; i < n; i++) {
        char c = (i + 1) % line_len == 0 ? '\n' : 'A' + (i % 26);
        if (fputc(c, f) == EOF) { fclose(f); return -1; }
    }
    fclose(f);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Scenarios                                                           */
/* ------------------------------------------------------------------ */

/* sc1: alternate calloc / free with fread.  AGGRESSIVE: 1M ops,
 * 4K live slots, canary check on EVERY op, sizes weighted near
 * 144 bytes (the size that triggered split_block on Xfbdev). */
#define SC1_FILE "/tmp/torture_heap_stdio_sc1"
static int sc1_calloc_fread_dance(void) {
    if (make_test_file(SC1_FILE, 8192, 32) != 0)
        FAIL("make_test_file: %s", strerror(errno));

    enum { LIVE_CAP = 512 };
    void  **live  = calloc(LIVE_CAP, sizeof(*live));
    size_t *sizes = calloc(LIVE_CAP, sizeof(*sizes));
    if (!live || !sizes) { unlink(SC1_FILE); FAIL("harness calloc"); }

    /* 100K ops with 512 live slots = 50K alloc/free pairs.  Sized
     * so a substrate QEMU-KVM run finishes in seconds (the
     * allocator is O(N) per op; 4K * 1M would be hours). */
    for (int op = 0; op < 100000; op++) {
        int slot = (int)(rng_next() % LIVE_CAP);
        if (live[slot]) {
            if (!canary_check(live[slot], sizes[slot])) {
                unlink(SC1_FILE);
                FAIL("canary corruption at op %d slot %d sz=%zu",
                     op, slot, sizes[slot]);
            }
            free(live[slot]); live[slot] = NULL;
        } else {
            /* Weight size distribution: 50% near-144, 40% small,
             * 10% larger (up to 4 KiB). */
            uint32_t r = rng_next() % 100;
            size_t sz;
            if (r < 50)      sz = rng_range(120, 168);
            else if (r < 90) sz = rng_range(1, 96);
            else             sz = rng_range(256, 4096);
            void *p = calloc(1, sz);
            if (!p) { unlink(SC1_FILE); FAIL("calloc(%zu) at op %d", sz, op); }
            canary_fill(p, sz);
            live[slot] = p; sizes[slot] = sz;
        }

        /* Frequent fopen+fread to flush whatever stdio state interacts. */
        if ((op & 0xFF) == 0) {
            FILE *f = fopen(SC1_FILE, "rb");
            if (!f) { unlink(SC1_FILE); FAIL("fopen at op %d: %s", op, strerror(errno)); }
            char buf[256];
            (void)fread(buf, 1, sizeof(buf), f);
            fclose(f);
        }

        /* Walk a random subset of live slots verifying canaries —
         * catches a corruption that lands on a NEIGHBOUR rather
         * than the just-touched slot. */
        if ((op & 0x3FF) == 0) {
            for (int k = 0; k < 32; k++) {
                int s = (int)(rng_next() % LIVE_CAP);
                if (live[s] && !canary_check(live[s], sizes[s])) {
                    unlink(SC1_FILE);
                    FAIL("neighbour canary at op %d slot %d sz=%zu", op, s, sizes[s]);
                }
            }
        }
    }

    for (int i = 0; i < LIVE_CAP; i++) {
        if (live[i]) {
            if (!canary_check(live[i], sizes[i])) {
                unlink(SC1_FILE);
                FAIL("final canary at slot %d sz=%zu", i, sizes[i]);
            }
            free(live[i]);
        }
    }
    free(live); free(sizes);
    unlink(SC1_FILE);
    return 0;
}

/* sc2: fgets line-by-line on a file made of mixed-length lines.
 * Reproduces the RegisterExtensionNames → fgets path that crashed
 * in fread's memcpy. */
#define SC2_FILE "/tmp/torture_heap_stdio_sc2"
static int sc2_fgets_lines(void) {
    /* File: 200 lines, varying length 1..255 chars. */
    FILE *w = fopen(SC2_FILE, "wb");
    if (!w) FAIL("fopen write: %s", strerror(errno));
    int lines = 200;
    for (int i = 0; i < lines; i++) {
        int len = 1 + (i * 13 + 7) % 250;
        for (int j = 0; j < len; j++) fputc('A' + (j % 26), w);
        fputc('\n', w);
    }
    fclose(w);

    /* Read it back line by line.  Buffer sized so most lines fit, some
     * don't — fgets re-loops via stdio internals on those. */
    FILE *r = fopen(SC2_FILE, "rb");
    if (!r) { unlink(SC2_FILE); FAIL("fopen read"); }
    char buf[64];
    int n_read = 0;
    while (fgets(buf, sizeof(buf), r) != NULL) {
        n_read++;
        size_t blen = strlen(buf);
        if (blen == 0) { fclose(r); unlink(SC2_FILE); FAIL("empty fgets result line %d", n_read); }
    }
    fclose(r);
    unlink(SC2_FILE);
    if (n_read < lines)
        FAIL("fgets returned %d times, expected at least %d full+continuation reads",
             n_read, lines);
    return 0;
}

/* sc3: realloc grow + shrink under stdio pressure.  Allocates a
 * buffer, grows it via realloc many times (which exercises
 * coalesce / new-block-and-copy paths), with fread interleaved to
 * keep heap state churning. */
#define SC3_FILE "/tmp/torture_heap_stdio_sc3"
static int sc3_realloc_under_io(void) {
    if (make_test_file(SC3_FILE, 4096, 40) != 0) FAIL("make_test_file");
    FILE *f = fopen(SC3_FILE, "rb");
    if (!f) { unlink(SC3_FILE); FAIL("fopen"); }

    size_t cap = 16;
    char *buf = malloc(cap);
    if (!buf) { fclose(f); unlink(SC3_FILE); FAIL("malloc"); }
    memset(buf, 'X', cap);

    for (int i = 0; i < 500; i++) {
        size_t newcap = (i & 1) ? cap * 2 : (cap > 32 ? cap / 2 : cap + 16);
        char *nb = realloc(buf, newcap);
        if (!nb) { free(buf); fclose(f); unlink(SC3_FILE); FAIL("realloc(%zu)", newcap); }
        buf = nb;
        /* Touch every byte to check that realloc preserved old
         * content where it should and gave us writable new bytes. */
        for (size_t k = 0; k < newcap; k++) buf[k] = 'Y';
        cap = newcap;

        if ((i & 31) == 0) {
            char rbuf[128];
            (void)fread(rbuf, 1, sizeof(rbuf), f);
            /* Rewind on EOF */
            if (feof(f)) { rewind(f); }
        }
    }
    free(buf); fclose(f); unlink(SC3_FILE);
    return 0;
}

/* sc4: tight fopen/fclose loop — exercises FILE struct alloc/free.
 * Tests heap's ability to reuse small blocks at high frequency. */
#define SC4_FILE "/tmp/torture_heap_stdio_sc4"
static int sc4_fopen_storm(void) {
    if (make_test_file(SC4_FILE, 1024, 32) != 0) FAIL("make_test_file");
    for (int i = 0; i < 50000; i++) {
        FILE *f = fopen(SC4_FILE, "rb");
        if (!f) { unlink(SC4_FILE); FAIL("fopen at i=%d: %s", i, strerror(errno)); }
        char c;
        size_t got = fread(&c, 1, 1, f);
        if (got != 1) { fclose(f); unlink(SC4_FILE); FAIL("fread at i=%d", i); }
        if (c != 'A') { fclose(f); unlink(SC4_FILE); FAIL("first byte %02x at i=%d", c, i); }
        fclose(f);
    }
    unlink(SC4_FILE);
    return 0;
}

/* sc5: ungetc near buffer boundaries.  ungetc() pushes back into
 * stdio's internal buffer; if the buffer is small/full it must grow
 * (or stash separately).  Exercises an under-tested malloc path. */
#define SC5_FILE "/tmp/torture_heap_stdio_sc5"
static int sc5_ungetc_dance(void) {
    if (make_test_file(SC5_FILE, 4096, 80) != 0) FAIL("make_test_file");
    FILE *f = fopen(SC5_FILE, "rb");
    if (!f) { unlink(SC5_FILE); FAIL("fopen"); }
    for (int i = 0; i < 500000; i++) {
        int c = fgetc(f);
        if (c == EOF) { rewind(f); continue; }
        if (ungetc(c, f) == EOF) { fclose(f); unlink(SC5_FILE); FAIL("ungetc"); }
        int c2 = fgetc(f);
        if (c2 != c) { fclose(f); unlink(SC5_FILE);
                       FAIL("ungetc didn't push back: got %d, expected %d", c2, c); }
    }
    fclose(f); unlink(SC5_FILE);
    return 0;
}

/* sc6: the X-server-shape pattern.  Mimic XkmInsureSize: many small
 * calloc()s for fixed-size records (32, 64, 128, 144, 256 bytes),
 * interspersed with file reads of varying sizes. */
#define SC6_FILE "/tmp/torture_heap_stdio_sc6"
static int sc6_xkb_shape(void) {
    /* Make a fake "xkm" file: 16 KiB of binary, lookalike keymap. */
    FILE *w = fopen(SC6_FILE, "wb");
    if (!w) FAIL("fopen write");
    char chunk[1024];
    for (size_t i = 0; i < sizeof(chunk); i++) chunk[i] = (char)(i & 0xFF);
    for (int i = 0; i < 16; i++) fwrite(chunk, 1, sizeof(chunk), w);
    fclose(w);

    /* The XKB-shape sequence: open file, read header, calloc record
     * array, read body, calloc another record array, etc. */
    const size_t calloc_sizes[] = { 32, 64, 128, 144, 256, 512 };
    enum { N_SIZES = sizeof(calloc_sizes)/sizeof(calloc_sizes[0]) };

    /* 5,000 rounds × 32 calloc + 32 fread chunks = 160K ops.
     * Sized so a substrate QEMU-KVM run finishes well under the
     * test budget (substrate's first-fit malloc is O(N) per op).
     * Free in mixed order (forward / reverse / random) to cover all
     * three split_block + coalesce_block code paths. */
    for (int round = 0; round < 5000; round++) {
        FILE *f = fopen(SC6_FILE, "rb");
        if (!f) { unlink(SC6_FILE); FAIL("fopen round %d", round); }

        char hdr[32];
        if (fread(hdr, 1, sizeof(hdr), f) != sizeof(hdr)) {
            fclose(f); unlink(SC6_FILE); FAIL("hdr read round %d", round);
        }

        enum { N_ALLOCS = 32 };
        void  *allocs[N_ALLOCS] = { 0 };
        size_t aszs[N_ALLOCS]   = { 0 };
        for (int k = 0; k < N_ALLOCS; k++) {
            size_t sz = calloc_sizes[(round + k) % N_SIZES];
            allocs[k] = calloc(1, sz);
            aszs[k]   = sz;
            if (!allocs[k]) {
                for (int j = 0; j < k; j++) free(allocs[j]);
                fclose(f); unlink(SC6_FILE);
                FAIL("calloc(%zu) round %d k %d", sz, round, k);
            }
            /* Canary the block so we can verify it survives the
             * subsequent calloc/fread churn until we free it. */
            canary_fill(allocs[k], sz);

            char rbuf[256];
            (void)fread(rbuf, 1, sizeof(rbuf), f);
        }

        /* Re-verify ALL canaries before freeing. */
        for (int k = 0; k < N_ALLOCS; k++) {
            if (!canary_check(allocs[k], aszs[k])) {
                fclose(f); unlink(SC6_FILE);
                FAIL("canary mismatch round %d k %d sz=%zu", round, k, aszs[k]);
            }
        }

        /* Free in one of three orders, rotating each round. */
        if (round % 3 == 0) {
            for (int k = 0; k < N_ALLOCS; k++) free(allocs[k]);
        } else if (round % 3 == 1) {
            for (int k = N_ALLOCS - 1; k >= 0; k--) free(allocs[k]);
        } else {
            /* Pseudo-random free order */
            int order[N_ALLOCS];
            for (int k = 0; k < N_ALLOCS; k++) order[k] = k;
            for (int k = N_ALLOCS - 1; k > 0; k--) {
                int j = (int)(rng_next() % (k + 1));
                int t = order[k]; order[k] = order[j]; order[j] = t;
            }
            for (int k = 0; k < N_ALLOCS; k++) free(allocs[order[k]]);
        }
        fclose(f);
    }
    unlink(SC6_FILE);
    return 0;
}

/* sc7: 144-byte alloc/free hammer.  The size that triggered the
 * split_block crash on Xfbdev.  Single-block alloc/free has O(1)
 * cost (no list walk needed — the previous free is at the head),
 * so this can stay at 1M iterations without timing out. */
static int sc7_alloc_free_cycle(void) {
    for (long i = 0; i < 1000000L; i++) {
        size_t sz = 144;
        void *p = malloc(sz);
        if (!p) FAIL("malloc at i=%ld", i);
        /* Touch first/last to make sure the block is fully writable. */
        ((volatile uint8_t *)p)[0] = 0xC3;
        ((volatile uint8_t *)p)[sz - 1] = 0x3C;
        free(p);
    }
    return 0;
}

/* sc8: large alloc + fread of large buffer.  Tests mmap-backed
 * request_space path. */
#define SC8_FILE "/tmp/torture_heap_stdio_sc8"
static int sc8_large_buffer(void) {
    if (make_test_file(SC8_FILE, 256 * 1024, 128) != 0) FAIL("make_test_file");
    FILE *f = fopen(SC8_FILE, "rb");
    if (!f) { unlink(SC8_FILE); FAIL("fopen"); }
    size_t bufsz = 64 * 1024;
    char *buf = malloc(bufsz);
    if (!buf) { fclose(f); unlink(SC8_FILE); FAIL("malloc %zu", bufsz); }
    size_t total = 0;
    for (;;) {
        size_t got = fread(buf, 1, bufsz, f);
        if (got == 0) break;
        total += got;
    }
    free(buf); fclose(f); unlink(SC8_FILE);
    if (total != 256 * 1024) FAIL("read %zu, expected %d", total, 256 * 1024);
    return 0;
}

/* sc9: SIGALRM during malloc/free.  Periodic 5ms timer over 2s of
 * heap churn.  When a canary mismatch happens we want detailed
 * forensics: which byte was wrong, what value it became, ±16 bytes
 * of context (so we can spot sigframe-like values: saved EIP in
 * code-segment range, segment selectors 0x1b/0x23, EFLAGS=0x202,
 * SIGALRM=14, etc.), and the block address (to compare against the
 * user stack range we'd expect a sigframe write to land in).  Keep
 * running after the first hit, up to MAX_REPORTS, so we can see
 * whether the corruption is at a consistent offset or scattered.
 *
 * Substrate baseline before the malloc lock: this scenario was
 * masked by the unlocked-heap crashes.  Now that the lock is in
 * place, the remaining bug shows: canary corruption after a couple
 * of SIGALRMs, well inside the live[] table. */
static volatile sig_atomic_t g_sc9_ticks;
static void sc9_handler(int sig) { (void)sig; g_sc9_ticks++; }

#define SC9_MAX_REPORTS 4

static void sc9_dump_corruption(long ops, int slot, void *p, size_t sz,
                                int off, uint8_t got, uint8_t want,
                                int ticks)
{
    uint8_t *b = p;
    fprintf(stderr,
        "  CORRUPT: op=%ld slot=%d ticks=%d block=%p sz=%zu off=%d "
        "got=0x%02x want=0x%02x\n",
        ops, slot, ticks, p, sz, off, got, want);

    /* Hex dump 32 bytes centred on the mismatch (clipped to block). */
    int lo = off - 16; if (lo < 0) lo = 0;
    int hi = off + 16; if (hi > (int)sz) hi = (int)sz;
    fprintf(stderr, "    bytes %d..%d:", lo, hi - 1);
    for (int i = lo; i < hi; i++) {
        fprintf(stderr, "%s%02x", i == off ? " *" : " ", b[i]);
    }
    fprintf(stderr, "\n");

    /* Also interpret the surrounding bytes as little-endian dwords;
     * sigframe contents (saved EIP, segment selectors, EFLAGS) would
     * show up here as recognizable 32-bit patterns. */
    int dlo = (off & ~3) - 8; if (dlo < 0) dlo = 0;
    int dhi = dlo + 24;       if (dhi > (int)sz - 3) dhi = (int)sz - 3;
    fprintf(stderr, "    dwords @off:");
    for (int i = dlo; i + 3 < dhi; i += 4) {
        uint32_t v = (uint32_t)b[i]      | (uint32_t)b[i+1] << 8 |
                     (uint32_t)b[i+2]<<16 | (uint32_t)b[i+3]<<24;
        fprintf(stderr, " [%d]=0x%08x", i, v);
    }
    fprintf(stderr, "\n");
}

static int sc9_use_ign = 0;
static int sc9_sigalrm_during_alloc(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sc9_use_ign ? SIG_IGN : sc9_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGALRM, &sa, NULL) < 0) FAIL("sigaction");
    fprintf(stderr, "  note: sc9 handler = %s\n",
            sc9_use_ign ? "SIG_IGN" : "sc9_handler");

    struct itimerval it = {
        .it_value    = { .tv_sec = 0, .tv_usec = 5000 },
        .it_interval = { .tv_sec = 0, .tv_usec = 5000 }
    };
    if (setitimer(ITIMER_REAL, &it, NULL) < 0) FAIL("setitimer");

    enum { LIVE_CAP = 1024 };
    void **live   = calloc(LIVE_CAP, sizeof(*live));
    size_t *sizes = calloc(LIVE_CAP, sizeof(*sizes));
    if (!live || !sizes) FAIL("harness calloc");

    fprintf(stderr, "  note: sc9 live array at %p, sizes at %p, "
                    "g_sc9_ticks at %p\n",
            (void *)live, (void *)sizes, (void *)&g_sc9_ticks);

    g_sc9_ticks = 0;
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    long ops = 0;
    int reports = 0;
    for (;;) {
        int slot = (int)(rng_next() % LIVE_CAP);
        if (live[slot]) {
            /* Walk the canary manually so we get the first mismatch
             * offset.  canary_check returns 0/1 only. */
            uint8_t *b = live[slot];
            size_t sz = sizes[slot];
            int bad_off = -1;
            uint8_t bad_got = 0, bad_want = 0;
            for (size_t i = 0; i < sz; i++) {
                uint8_t want = canary_byte(live[slot], i);
                if (b[i] != want) {
                    bad_off = (int)i; bad_got = b[i]; bad_want = want;
                    break;
                }
            }
            if (bad_off >= 0) {
                sc9_dump_corruption(ops, slot, live[slot], sz,
                                    bad_off, bad_got, bad_want,
                                    (int)g_sc9_ticks);
                if (++reports >= SC9_MAX_REPORTS) {
                    /* Disarm timer + drain + fail */
                    memset(&it, 0, sizeof(it));
                    setitimer(ITIMER_REAL, &it, NULL);
                    for (int i = 0; i < LIVE_CAP; i++) if (live[i]) free(live[i]);
                    free(live); free(sizes);
                    FAIL("hit SC9_MAX_REPORTS (%d) corruptions",
                         SC9_MAX_REPORTS);
                }
                /* Refill so subsequent canary_check on this block
                 * passes (we want to keep going). */
                canary_fill(live[slot], sz);
            }
            free(live[slot]); live[slot] = NULL;
        } else {
            size_t sz = rng_range(8, 1024);
            void *p = malloc(sz);
            if (!p) FAIL("malloc(%zu) at op %ld", sz, ops);
            canary_fill(p, sz);
            live[slot] = p; sizes[slot] = sz;
        }
        ops++;
        if ((ops & 0xFFFF) == 0) {
            clock_gettime(CLOCK_MONOTONIC, &t1);
            long ms = (t1.tv_sec - t0.tv_sec) * 1000 +
                      (t1.tv_nsec - t0.tv_nsec) / 1000000;
            if (ms >= 2000) break;
        }
    }
    memset(&it, 0, sizeof(it));
    setitimer(ITIMER_REAL, &it, NULL);
    for (int i = 0; i < LIVE_CAP; i++) if (live[i]) free(live[i]);
    free(live); free(sizes);
    fprintf(stderr, "  note: sc9 did %ld ops in 2s with %d SIGALRMs, %d corruptions\n",
            ops, (int)g_sc9_ticks, reports);
    return reports > 0 ? 1 : 0;
}

/* sc9b: same as sc9 but installs SIG_IGN.  Discriminator: does the
 * corruption persist when no user-mode handler runs? */
static int sc9b_sigalrm_ign(void) {
    sc9_use_ign = 1;
    int r = sc9_sigalrm_during_alloc();
    sc9_use_ign = 0;
    return r;
}

/* sc9c: minimal trace mode.  Print every malloc with the block
 * address and size, plus a marker for each SIGALRM, so we can
 * line up the kernel's XSIG sigframe-destination log with the
 * userland heap layout to see whether sendsig is writing into a
 * live allocation. */
static void sc9c_handler(int sig) {
    (void)sig;
    /* Async-signal-safe: write() is on the POSIX safe list; printf
     * is not.  Marker lines up against the kernel's xsig log. */
    static const char marker[] = "  TICK\n";
    write(2, marker, sizeof(marker) - 1);
    g_sc9_ticks++;
}

static int sc9c_trace_mode(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sc9c_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGALRM, &sa, NULL) < 0) FAIL("sigaction");

    struct itimerval it = {
        .it_value    = { .tv_sec = 0, .tv_usec = 5000 },
        .it_interval = { .tv_sec = 0, .tv_usec = 5000 }
    };
    if (setitimer(ITIMER_REAL, &it, NULL) < 0) FAIL("setitimer");

    /* Track every live block; print address + size for each
     * malloc, so the kernel xsig log can be correlated. */
    enum { LIVE_CAP = 64 };           /* small set so the log is readable */
    void *live[LIVE_CAP] = { 0 };
    size_t sizes[LIVE_CAP] = { 0 };

    g_sc9_ticks = 0;
    long ops = 0;
    int reports = 0;
    /* Stop conditions: 3 corruptions seen OR 10 ticks elapsed —
     * we just need a small slice of data, not 2 seconds of it. */
    while (g_sc9_ticks < 10 && reports < 3 && ops < 5000) {
        int slot = (int)(rng_next() % LIVE_CAP);
        if (live[slot]) {
            uint8_t *b = live[slot];
            size_t sz = sizes[slot];
            int bad_off = -1;
            uint8_t bad_got = 0, bad_want = 0;
            for (size_t i = 0; i < sz; i++) {
                uint8_t want = canary_byte(live[slot], i);
                if (b[i] != want) {
                    bad_off = (int)i; bad_got = b[i]; bad_want = want;
                    break;
                }
            }
            if (bad_off >= 0) {
                sc9_dump_corruption(ops, slot, live[slot], sz,
                                    bad_off, bad_got, bad_want,
                                    (int)g_sc9_ticks);
                reports++;
                canary_fill(live[slot], sz);
            }
            fprintf(stderr, "  FREE  slot=%d ptr=%p sz=%zu\n",
                    slot, live[slot], sz);
            free(live[slot]); live[slot] = NULL;
        } else {
            size_t sz = rng_range(64, 256);
            void *p = malloc(sz);
            if (!p) FAIL("malloc(%zu) at op %ld", sz, ops);
            canary_fill(p, sz);
            fprintf(stderr, "  ALLOC slot=%d ptr=%p sz=%zu\n",
                    slot, p, sz);
            live[slot] = p; sizes[slot] = sz;
        }
        ops++;
    }
    memset(&it, 0, sizeof(it));
    setitimer(ITIMER_REAL, &it, NULL);
    for (int i = 0; i < LIVE_CAP; i++) if (live[i]) free(live[i]);
    fprintf(stderr, "  note: sc9c did %ld ops, %d ticks, %d corruptions\n",
            ops, (int)g_sc9_ticks, reports);
    return reports > 0 ? 1 : 0;
}

/* sc9e: verify-on-write canary fill.  After writing each byte,
 * immediately re-read and confirm.  If the corruption is happening
 * DURING canary_fill (because a SIGALRM lands mid-loop and
 * something on the kernel return path zaps a byte we just wrote),
 * we'll catch it within microseconds of the write rather than at
 * the next canary_check long after.  If the corruption only
 * appears LATER (between fill and check), sc9e will still PASS
 * locally and we know it's a delayed effect. */
static int sc9e_verify_on_write(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sc9_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGALRM, &sa, NULL) < 0) FAIL("sigaction");

    struct itimerval it = {
        .it_value    = { .tv_sec = 0, .tv_usec = 5000 },
        .it_interval = { .tv_sec = 0, .tv_usec = 5000 }
    };
    if (setitimer(ITIMER_REAL, &it, NULL) < 0) FAIL("setitimer");

    g_sc9_ticks = 0;
    int reports = 0;
    long ops = 0;
    /* Allocate a 1 KiB block ONCE; reuse forever.  Fill it byte by
     * byte, verifying each write.  If the verify fails, the
     * corruption happened in the very small window between the
     * STORE instruction and the immediately following LOAD. */
    uint8_t *p = malloc(1024);
    if (!p) FAIL("malloc");

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    long ms = 0;
    while (ms < 2000 && reports < SC9_MAX_REPORTS) {
        for (size_t i = 0; i < 1024; i++) {
            uint8_t want = canary_byte(p, i);
            p[i] = want;
            /* Read back immediately.  Use `volatile` to defeat the
             * optimiser folding the load with the just-done store. */
            uint8_t got = ((volatile uint8_t *)p)[i];
            if (got != want) {
                sc9_dump_corruption(ops, /*slot*/-1, p, 1024,
                                    (int)i, got, want, (int)g_sc9_ticks);
                reports++;
                if (reports >= SC9_MAX_REPORTS) break;
                /* Restore so we can keep going. */
                p[i] = want;
            }
        }
        ops++;
        if ((ops & 0x3F) == 0) {
            clock_gettime(CLOCK_MONOTONIC, &t1);
            ms = (t1.tv_sec - t0.tv_sec) * 1000 +
                 (t1.tv_nsec - t0.tv_nsec) / 1000000;
        }
    }
    memset(&it, 0, sizeof(it));
    setitimer(ITIMER_REAL, &it, NULL);
    fprintf(stderr, "  note: sc9e did %ld fills (1KB each) with %d SIGALRMs, %d immediate mismatches\n",
            ops, (int)g_sc9_ticks, reports);
    free(p);
    return reports > 0 ? 1 : 0;
}

/* sc9d: handler that calls _exit(42) on first SIGALRM.  If the
 * corruption happens during the iret-to-handler transition (i.e.
 * before the handler body runs at all), we'll still see byte
 * damage in the live blocks visible to the EXIT path.  But the
 * test's main process forked us, and we _exit from a signal
 * handler — async-signal-safe — so the child status reflects
 * whether we got to _exit cleanly or crashed before. */
static void sc9d_exit_handler(int sig) {
    (void)sig;
    _exit(42);   /* async-signal-safe per POSIX */
}

static int sc9d_exit_in_handler(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sc9d_exit_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGALRM, &sa, NULL) < 0) FAIL("sigaction");

    struct itimerval it = {
        .it_value    = { .tv_sec = 0, .tv_usec = 50000 },
        .it_interval = { .tv_sec = 0, .tv_usec = 0 }
    };
    if (setitimer(ITIMER_REAL, &it, NULL) < 0) FAIL("setitimer");

    /* Pre-allocate a block + canary it.  Then spin for >50ms so the
     * timer fires.  The exit handler _exit(42)s the child without
     * touching any userland state, so the parent's waitpid sees
     * status 42 if the iret-to-handler-PC transition itself is
     * clean.  A crash before _exit indicates iret corrupts state. */
    void *p = malloc(512);
    if (!p) FAIL("malloc");
    canary_fill(p, 512);
    fprintf(stderr, "  pre-spin: block at %p, sz=512\n", p);

    /* Spin until the timer fires us out via _exit. */
    for (volatile long i = 0; i < 100000000L; i++) {
        if (i % 1000000 == 0) {
            /* Sanity: are we still here? */
            if (((volatile uint8_t *)p)[0] != canary_byte(p, 0)) {
                fprintf(stderr, "  CORRUPT mid-spin: byte0 changed before exit\n");
                free(p);
                FAIL("byte0 corruption pre-exit");
            }
        }
    }
    /* If we get here, the handler never fired in 100M loop iters —
     * means the timer didn't deliver.  Counts as a test failure. */
    free(p);
    FAIL("timer didn't fire — handler never invoked");
}

/* sc10: adversarial fragmentation.  Alternating big + small allocs,
 * then free every-other big to leave a sawtooth heap shape.  Forces
 * split_block to land on tiny gaps right next to live blocks. */
static int sc10_fragmentation_storm(void) {
    enum { N = 8192 };
    void **bigs   = calloc(N, sizeof(void *));
    void **smalls = calloc(N, sizeof(void *));
    if (!bigs || !smalls) FAIL("harness calloc");

    /* Phase 1: alternate-allocate big + small */
    for (int i = 0; i < N; i++) {
        bigs[i]   = malloc(512);
        smalls[i] = malloc(48);
        if (!bigs[i] || !smalls[i]) FAIL("phase1 alloc at i=%d", i);
        memset(bigs[i], 0xAA, 512);
        memset(smalls[i], 0x55, 48);
    }
    /* Phase 2: free every other big, leaving sawtooth */
    for (int i = 0; i < N; i += 2) {
        free(bigs[i]); bigs[i] = NULL;
    }
    /* Phase 3: try to fit 144-byte blocks into the 512-byte gaps */
    void **fillers = calloc(N, sizeof(void *));
    if (!fillers) FAIL("harness calloc");
    for (int i = 0; i < N; i++) {
        fillers[i] = malloc(144);
        if (!fillers[i]) FAIL("phase3 alloc at i=%d", i);
        memset(fillers[i], 0xC3, 144);
    }
    /* Phase 4: drain everything in chaotic order */
    for (int i = 0; i < N; i++) {
        if (bigs[i])   { free(bigs[i]);   bigs[i] = NULL; }
        free(fillers[i]); fillers[i] = NULL;
        free(smalls[i]); smalls[i] = NULL;
    }
    free(bigs); free(smalls); free(fillers);
    return 0;
}

/* sc11: realloc storm — repeatedly grow + shrink a single buffer.
 * Hits realloc's in-place-grow / copy / shrink-then-split paths. */
static int sc11_realloc_storm(void) {
    size_t sz = 16;
    char *buf = malloc(sz);
    if (!buf) FAIL("initial malloc");
    memset(buf, 0x77, sz);

    for (int i = 0; i < 500000; i++) {
        size_t newsz;
        uint32_t r = rng_next() & 0x7;
        switch (r) {
            case 0: newsz = sz * 2;  break;          /* grow 2x */
            case 1: newsz = sz / 2;  break;          /* shrink half */
            case 2: newsz = sz + 1;  break;          /* grow 1 byte */
            case 3: newsz = sz + 16; break;          /* grow 16 byte */
            case 4: newsz = 144;     break;          /* hop to magic size */
            case 5: newsz = 1024;    break;
            case 6: newsz = 32;      break;
            default: newsz = sz;     break;
        }
        if (newsz < 16)    newsz = 16;
        if (newsz > 65536) newsz = 65536;
        char *nb = realloc(buf, newsz);
        if (!nb) { free(buf); FAIL("realloc(%zu) at i=%d", newsz, i); }
        buf = nb;
        /* Touch first & last to verify writable. */
        buf[0]         = (char)(i & 0xFF);
        buf[newsz - 1] = (char)(~i & 0xFF);
        sz = newsz;
    }
    free(buf);
    return 0;
}

/* sc12: fork storm — fork a child, child does heavy alloc, exit;
 * repeat 200 times.  Catches "first-allocations-after-fork-corrupt"
 * patterns (Xfbdev is forked from init; the crash is at startup). */
static int sc12_fork_storm(void) {
    for (int round = 0; round < 200; round++) {
        pid_t kid = fork();
        if (kid < 0) FAIL("fork at round %d", round);
        if (kid == 0) {
            /* Child: replicate Xfbdev startup-shape allocation
             * pattern as fast as possible. */
            const size_t sizes[] = { 16, 32, 48, 96, 128, 144, 256, 512 };
            const int N = sizeof(sizes) / sizeof(sizes[0]);
            void *live[256] = { 0 };
            for (int i = 0; i < 5000; i++) {
                int slot = i % 256;
                if (live[slot]) free(live[slot]);
                size_t sz = sizes[i % N];
                live[slot] = malloc(sz);
                if (!live[slot]) _exit(2);
                memset(live[slot], 0xFE, sz);
            }
            for (int i = 0; i < 256; i++) if (live[i]) free(live[i]);
            _exit(0);
        }
        int status;
        waitpid(kid, &status, 0);
        if (WIFSIGNALED(status))
            FAIL("child round %d died on signal %d", round, WTERMSIG(status));
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
            FAIL("child round %d bad exit (status=0x%x)", round, status);
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Driver                                                              */
/* ------------------------------------------------------------------ */

struct sc { const char *name; int (*fn)(void); };
static struct sc tests[] = {
    { "sc1_calloc_fread_dance",     sc1_calloc_fread_dance },
    { "sc2_fgets_lines",            sc2_fgets_lines        },
    { "sc3_realloc_under_io",       sc3_realloc_under_io   },
    { "sc4_fopen_storm",            sc4_fopen_storm        },
    { "sc5_ungetc_dance",           sc5_ungetc_dance       },
    { "sc6_xkb_shape",              sc6_xkb_shape          },
    { "sc7_alloc_free_cycle",       sc7_alloc_free_cycle   },
    { "sc8_large_buffer",           sc8_large_buffer       },
    { "sc9_sigalrm_during_alloc",   sc9_sigalrm_during_alloc },
    { "sc9b_sigalrm_ign",           sc9b_sigalrm_ign       },
    { "sc9c_trace_mode",            sc9c_trace_mode        },
    { "sc9d_exit_in_handler",       sc9d_exit_in_handler   },
    { "sc9e_verify_on_write",       sc9e_verify_on_write   },
    { "sc10_fragmentation_storm",   sc10_fragmentation_storm },
    { "sc11_realloc_storm",         sc11_realloc_storm     },
    { "sc12_fork_storm",            sc12_fork_storm        },
};

/* Each scenario runs in a forked child so a crash localizes. */
static int run_scenario(size_t i) {
    pid_t kid = fork();
    if (kid < 0) { fprintf(stderr, "  fork failed: %s\n", strerror(errno)); return 1; }
    if (kid == 0) {
        int r = tests[i].fn();
        _exit(r ? 1 : 0);
    }
    int status;
    if (waitpid(kid, &status, 0) < 0) { fprintf(stderr, "  waitpid: %s\n", strerror(errno)); return 1; }
    if (WIFSIGNALED(status)) {
        fprintf(stderr, "  CHILD died on signal %d\n", WTERMSIG(status));
        return 2;
    }
    if (!WIFEXITED(status)) return 1;
    return WEXITSTATUS(status) == 0 ? 0 : 1;
}

int main(int argc, char **argv) {
    /* Optional: argv[1] is a scenario-name substring or "all".
     * Useful for the substrate test cycle where sc6's heavy canary
     * work + locked allocator eats the 45s budget by itself. */
    const char *only = (argc > 1) ? argv[1] : NULL;
    if (only && !strcmp(only, "all")) only = NULL;

    int pass = 0, fail = 0, crash = 0;
    for (size_t i = 0; i < sizeof(tests)/sizeof(tests[0]); i++) {
        if (only && !strstr(tests[i].name, only)) continue;
        fprintf(stderr, "[%s] running...\n", tests[i].name);
        int r = run_scenario(i);
        if      (r == 0) { pass++;  fprintf(stderr, "[%s] PASS\n",  tests[i].name); }
        else if (r == 2) { crash++; fprintf(stderr, "[%s] CRASH\n", tests[i].name); }
        else             { fail++;  fprintf(stderr, "[%s] FAIL\n",  tests[i].name); }
    }
    fprintf(stderr, "\ntorture_heap_stdio: %d PASS, %d FAIL, %d CRASH\n",
            pass, fail, crash);
    printf("Result: %s (%d/%d PASS, %d crash)\n",
           (fail + crash) ? "FAILED" : "PASSED",
           pass, pass + fail + crash, crash);
    fflush(stdout);
    return (fail + crash) ? 1 : 0;
}
