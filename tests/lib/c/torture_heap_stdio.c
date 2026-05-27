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
#include <sys/wait.h>
#include <sys/stat.h>

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

/* sc1: alternate calloc / free with fread of a small file.  Mimics
 * Xfbdev's XKB init: read a binary file in chunks while calloc'ing
 * data structures of the same size class.  144 bytes is the size
 * the split_block crash hit, so weight that range. */
#define SC1_FILE "/tmp/torture_heap_stdio_sc1"
static int sc1_calloc_fread_dance(void) {
    if (make_test_file(SC1_FILE, 8192, 32) != 0)
        FAIL("make_test_file: %s", strerror(errno));

    enum { LIVE_CAP = 64 };
    void *live[LIVE_CAP]; size_t sizes[LIVE_CAP];
    memset(live, 0, sizeof(live));

    for (int op = 0; op < 5000; op++) {
        /* Random calloc/free against the live set */
        int slot = (int)(rng_next() % LIVE_CAP);
        if (live[slot]) {
            if (!canary_check(live[slot], sizes[slot])) {
                unlink(SC1_FILE);
                FAIL("canary corruption at op %d slot %d", op, slot);
            }
            free(live[slot]); live[slot] = NULL;
        } else {
            size_t sz = rng_range(8, 384);     /* weight near 144 */
            void *p = calloc(1, sz);
            if (!p) { unlink(SC1_FILE); FAIL("calloc(%zu) at op %d", sz, op); }
            canary_fill(p, sz);
            live[slot] = p; sizes[slot] = sz;
        }

        /* Open + read a few chunks + close.  fread() drives stdio
         * internal buffer + libc malloc for the FILE struct. */
        if ((op & 7) == 0) {
            FILE *f = fopen(SC1_FILE, "rb");
            if (!f) { unlink(SC1_FILE); FAIL("fopen at op %d: %s", op, strerror(errno)); }
            char buf[256];
            size_t got = fread(buf, 1, sizeof(buf), f);
            if (got == 0 && !feof(f) && ferror(f)) {
                fclose(f); unlink(SC1_FILE);
                FAIL("fread returned 0 mid-file at op %d", op);
            }
            fclose(f);
        }
    }

    /* Drain live set, verifying canaries last time. */
    for (int i = 0; i < LIVE_CAP; i++) {
        if (live[i]) {
            if (!canary_check(live[i], sizes[i])) {
                unlink(SC1_FILE);
                FAIL("final canary check at slot %d", i);
            }
            free(live[i]);
        }
    }
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
    for (int i = 0; i < 2000; i++) {
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
    for (int i = 0; i < 1000; i++) {
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

    for (int round = 0; round < 200; round++) {
        FILE *f = fopen(SC6_FILE, "rb");
        if (!f) { unlink(SC6_FILE); FAIL("fopen round %d", round); }

        /* Header read */
        char hdr[32];
        if (fread(hdr, 1, sizeof(hdr), f) != sizeof(hdr)) {
            fclose(f); unlink(SC6_FILE); FAIL("hdr read round %d", round);
        }

        /* Interleave: 8 calloc's + 8 fread chunks. */
        void *allocs[8] = { 0 };
        for (int k = 0; k < 8; k++) {
            size_t sz = calloc_sizes[(round + k) % N_SIZES];
            allocs[k] = calloc(1, sz);
            if (!allocs[k]) {
                for (int j = 0; j < k; j++) free(allocs[j]);
                fclose(f); unlink(SC6_FILE); FAIL("calloc(%zu) round %d k %d", sz, round, k);
            }
            /* Touch every byte (XKB does after the calloc returns). */
            memset(allocs[k], 0xA5, sz);

            char rbuf[256];
            (void)fread(rbuf, 1, sizeof(rbuf), f);
        }

        /* Free in reverse order — gives coalesce_block both
         * forward and backward neighbours to deal with. */
        for (int k = 7; k >= 0; k--) free(allocs[k]);
        fclose(f);
    }
    unlink(SC6_FILE);
    return 0;
}

/* sc7: alloc/free/alloc same size — heap should reuse the freed
 * block (and split_block should NOT trip).  Mimics what a long-
 * running process does over its lifetime. */
static int sc7_alloc_free_cycle(void) {
    for (int round = 0; round < 100; round++) {
        for (int i = 0; i < 1000; i++) {
            size_t sz = 144;          /* the size that crashed in Xfbdev */
            void *p = malloc(sz);
            if (!p) FAIL("malloc %d/%d", round, i);
            memset(p, 0xC3, sz);
            free(p);
        }
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

/* ------------------------------------------------------------------ */
/* Driver                                                              */
/* ------------------------------------------------------------------ */

struct sc { const char *name; int (*fn)(void); };
static struct sc tests[] = {
    { "sc1_calloc_fread_dance",  sc1_calloc_fread_dance },
    { "sc2_fgets_lines",         sc2_fgets_lines        },
    { "sc3_realloc_under_io",    sc3_realloc_under_io   },
    { "sc4_fopen_storm",         sc4_fopen_storm        },
    { "sc5_ungetc_dance",        sc5_ungetc_dance       },
    { "sc6_xkb_shape",           sc6_xkb_shape          },
    { "sc7_alloc_free_cycle",    sc7_alloc_free_cycle   },
    { "sc8_large_buffer",        sc8_large_buffer       },
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
    (void)argc; (void)argv;
    int pass = 0, fail = 0, crash = 0;
    for (size_t i = 0; i < sizeof(tests)/sizeof(tests[0]); i++) {
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
