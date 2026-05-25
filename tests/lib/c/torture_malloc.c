/*
 * torture_malloc.c — heap allocator torture test.
 *
 * Substrate's libc malloc has been the likely culprit behind a cascade
 * of mysterious crashes in third-party userland (Xfbdev, mandoc,
 * zsh).  This test hammers every code path of lib/c/src/stdlib.c:
 *
 *   - first-fit free-list walk via search_hint cache
 *   - split_block on oversized free blocks
 *   - coalesce_block on free of an adjacent neighbour
 *   - realloc grow (with adjacent-free fast path) and shrink (split)
 *   - calloc zero-init
 *   - magic-check on free() of garbage pointer
 *   - mmap'd region growth in request_space
 *
 * Each scenario uses a deterministic seed so a failing run is
 * reproducible.  Every live allocation carries a payload canary
 * derived from (ptr, size, alloc_id) and is re-verified at random
 * intervals; any neighbour-overrun, double-allocation, or split
 * mis-bookkeeping shows up as a canary mismatch with the offending
 * scenario / op-index logged.
 *
 * Portable: builds on Linux / FreeBSD / macOS against host libc
 * (baseline — must always pass) and cross-builds against substrate's
 * libc (where the real bugs live).
 *
 *   run: torture_malloc [scenario] [iterations]
 *        torture_malloc all
 *        torture_malloc storm 1000000
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>

#define MAX_LIVE       1024
#define DEFAULT_ITERS  100000
#define MAX_ALLOC      (256 * 1024)
#define BIG_ALLOC      (256 * 1024)   /* keep peak live < ~30 MiB so
                                         the test fits in a 128 MiB
                                         QEMU build; substrate's
                                         vm_fault returns confusing
                                         SIGSEGV under physical-mem
                                         oversubscription (no swap),
                                         which masks real malloc
                                         bugs.  Real-world heaviest
                                         font buffer is ~150 KiB. */

/* Deterministic xorshift32 — same sequence on host + substrate. */
static uint32_t rng_state = 0x1337BEEF;
static void rng_seed(uint32_t s) { rng_state = s ? s : 0x1337BEEF; }
static uint32_t rng_next(void) {
    uint32_t x = rng_state;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return rng_state = x;
}
static size_t rng_size(size_t max) {
    return (max == 0) ? 0 : (size_t)(rng_next() % max);
}

/* Live-allocation table. */
struct live {
    void   *ptr;
    size_t  size;
    uint32_t seed;        /* canary seed */
    int     alloc_id;     /* monotonic alloc number for traceability */
};
static struct live live[MAX_LIVE];
static int live_count;
static int alloc_counter;

/* Stats. */
static struct {
    long mallocs, callocs, reallocs, frees;
    long canary_checks, canary_failures;
    long oom, double_frees, magic_violations;
    long peak_live, peak_bytes;
    long cur_bytes;
} stats;

static int verbose;

/* --- canary -------------------------------------------------------- */

/* Per-allocation pattern derived from (seed, byte_offset).  Deterministic
 * given (seed) so we don't need to store the full payload to verify. */
static uint8_t canary_byte(uint32_t seed, size_t off) {
    uint32_t x = seed ^ (uint32_t)off;
    x = (x * 2654435761u) ^ (x >> 16);
    return (uint8_t)(x & 0xff);
}

static void canary_fill(void *p, size_t n, uint32_t seed) {
    uint8_t *b = (uint8_t *)p;
    for (size_t i = 0; i < n; i++) b[i] = canary_byte(seed, i);
}

static int canary_check(const void *p, size_t n, uint32_t seed,
                        int alloc_id, const char *where) {
    const uint8_t *b = (const uint8_t *)p;
    stats.canary_checks++;
    for (size_t i = 0; i < n; i++) {
        uint8_t want = canary_byte(seed, i);
        if (b[i] != want) {
            fprintf(stderr,
                "torture_malloc: CANARY FAIL in %s — "
                "alloc_id=%d ptr=%p size=%zu off=%zu want=0x%02x got=0x%02x\n",
                where, alloc_id, p, n, i, want, b[i]);
            /* Show a hex dump around the corruption. */
            size_t lo = i > 16 ? i - 16 : 0;
            size_t hi = (i + 16 < n) ? i + 16 : n;
            fprintf(stderr, "  hexdump [%zu..%zu]:", lo, hi);
            for (size_t j = lo; j < hi; j++) {
                fprintf(stderr, "%s%02x",
                        j == i ? " >" : " ", b[j]);
            }
            fprintf(stderr, "\n");
            stats.canary_failures++;
            return 0;
        }
    }
    return 1;
}

/* --- table mgmt ---------------------------------------------------- */

static void live_track(void *p, size_t n, uint32_t seed) {
    if (live_count >= MAX_LIVE) {
        /* table full — overwrite the slot we'd pick to free next */
        int victim = (int)(rng_next() % MAX_LIVE);
        free(live[victim].ptr);
        stats.frees++;
        stats.cur_bytes -= (long)live[victim].size;
        live[victim].ptr = p;
        live[victim].size = n;
        live[victim].seed = seed;
        live[victim].alloc_id = ++alloc_counter;
    } else {
        live[live_count].ptr = p;
        live[live_count].size = n;
        live[live_count].seed = seed;
        live[live_count].alloc_id = ++alloc_counter;
        live_count++;
    }
    stats.cur_bytes += (long)n;
    if (live_count > stats.peak_live) stats.peak_live = live_count;
    if (stats.cur_bytes > stats.peak_bytes) stats.peak_bytes = stats.cur_bytes;
}

static int live_pick(void) {
    if (live_count == 0) return -1;
    return (int)(rng_next() % (uint32_t)live_count);
}

static void live_remove(int idx) {
    if (idx < 0 || idx >= live_count) return;
    stats.cur_bytes -= (long)live[idx].size;
    live[idx] = live[live_count - 1];
    live_count--;
}

/* --- ops ----------------------------------------------------------- */

static void *do_malloc(size_t n) {
    void *p = malloc(n);
    stats.mallocs++;
    if (!p) {
        if (n != 0) stats.oom++;
        return NULL;
    }
    uint32_t seed = (uint32_t)((uintptr_t)p ^ (uintptr_t)n ^ rng_next());
    canary_fill(p, n, seed);
    live_track(p, n, seed);
    return p;
}

static void *do_calloc(size_t n) {
    void *p = calloc(1, n);
    stats.callocs++;
    if (!p) {
        if (n != 0) stats.oom++;
        return NULL;
    }
    /* Verify zero-init. */
    uint8_t *b = (uint8_t *)p;
    for (size_t i = 0; i < n; i++) {
        if (b[i] != 0) {
            fprintf(stderr,
                "torture_malloc: calloc NOT ZEROED ptr=%p n=%zu off=%zu got=0x%02x\n",
                p, n, i, b[i]);
            stats.canary_failures++;
            break;
        }
    }
    uint32_t seed = (uint32_t)((uintptr_t)p ^ (uintptr_t)n ^ rng_next());
    canary_fill(p, n, seed);
    live_track(p, n, seed);
    return p;
}

static void do_realloc_at(int idx, size_t new_size) {
    if (idx < 0 || idx >= live_count) return;
    void *old = live[idx].ptr;
    size_t old_size = live[idx].size;
    uint32_t old_seed = live[idx].seed;
    int old_id = live[idx].alloc_id;

    /* Pre-verify old buffer before we hand it back to realloc. */
    if (!canary_check(old, old_size, old_seed, old_id, "realloc-pre")) {
        /* corrupt already — bail */
        return;
    }

    void *np = realloc(old, new_size);
    stats.reallocs++;

    if (np == NULL && new_size != 0) {
        stats.oom++;
        /* old still valid per POSIX */
        return;
    }

    if (new_size == 0) {
        /* substrate's realloc treats size=0 as free() */
        stats.cur_bytes -= (long)old_size;
        live[idx] = live[live_count - 1];
        live_count--;
        return;
    }

    /* Verify retained prefix. */
    size_t verify = old_size < new_size ? old_size : new_size;
    if (!canary_check(np, verify, old_seed, old_id, "realloc-post")) {
        /* corrupt — leave entry and let the rest of the test
         * notice the cascade */
    }

    /* Re-canary entire new buffer with a fresh seed. */
    uint32_t seed = (uint32_t)((uintptr_t)np ^ (uintptr_t)new_size ^ rng_next());
    canary_fill(np, new_size, seed);

    stats.cur_bytes += (long)new_size - (long)old_size;
    live[idx].ptr = np;
    live[idx].size = new_size;
    live[idx].seed = seed;
    /* alloc_id retained — same logical allocation */
    if (stats.cur_bytes > stats.peak_bytes) stats.peak_bytes = stats.cur_bytes;
}

static void do_free_at(int idx) {
    if (idx < 0 || idx >= live_count) return;
    canary_check(live[idx].ptr, live[idx].size,
                 live[idx].seed, live[idx].alloc_id, "free-pre");
    free(live[idx].ptr);
    stats.frees++;
    live_remove(idx);
}

static void verify_random_subset(int n) {
    for (int i = 0; i < n; i++) {
        int idx = live_pick();
        if (idx < 0) break;
        canary_check(live[idx].ptr, live[idx].size,
                     live[idx].seed, live[idx].alloc_id, "periodic");
    }
}

static void free_all(void) {
    while (live_count > 0) {
        do_free_at(live_count - 1);
    }
}

/* --- scenarios ----------------------------------------------------- */

static void scenario_sizes(long iters) {
    (void)iters;
    /* sweep every size from 0 to MAX_ALLOC in coarse steps, alloc +
     * verify + free, sequentially.  Catches off-by-one in ALIGN /
     * split_block on specific size boundaries. */
    size_t sizes[] = {
        0, 1, 7, 8, 9, 15, 16, 17, 23, 24, 25, 31, 32, 33,
        47, 48, 49, 63, 64, 65, 127, 128, 129, 255, 256, 257,
        511, 512, 513, 1023, 1024, 1025, 2047, 2048, 2049,
        4095, 4096, 4097, 8191, 8192, 16383, 16384, 32768,
        65536, 131072, 262144,
    };
    int N = (int)(sizeof sizes / sizeof sizes[0]);
    for (int i = 0; i < N; i++) {
        void *p = do_malloc(sizes[i]);
        if (!p && sizes[i] != 0) {
            fprintf(stderr, "  sizes[%d]: malloc(%zu) failed\n", i, sizes[i]);
            continue;
        }
        verify_random_subset(2);
        do_free_at(live_count - 1);
    }
    /* Then again interleaved: alloc all sizes, then free all. */
    for (int i = 0; i < N; i++) do_malloc(sizes[i]);
    verify_random_subset(N);
    free_all();
}

static void scenario_storm(long iters) {
    /* Random mix of malloc/calloc/realloc/free with realistic small-
     * to-medium sizes.  Catches free-list corruption from interleaved
     * split/coalesce. */
    for (long i = 0; i < iters; i++) {
        uint32_t op = rng_next() % 100;
        if (op < 35) {
            do_malloc(rng_size(8192) + 1);
        } else if (op < 50) {
            do_calloc(rng_size(4096) + 1);
        } else if (op < 70 && live_count > 0) {
            do_realloc_at(live_pick(), rng_size(16384) + 1);
        } else if (live_count > 0) {
            do_free_at(live_pick());
        } else {
            do_malloc(rng_size(2048) + 1);
        }
        if ((i & 0xff) == 0) verify_random_subset(8);
    }
    free_all();
}

static void scenario_bigsmall(long iters) {
    /* Alternate small (<= 64) and large allocs.  Forces heavy
     * split + coalesce traffic.  Sizes tuned to fit a 128 MiB
     * QEMU build (peak live ~30 MiB), since substrate's vm_fault
     * surfaces physical-memory exhaustion as a confusing SIGSEGV
     * with trap_addr=0 instead of a clear OOM signal.  That's a
     * substrate VM bug worth fixing separately, but it masks
     * real malloc bugs in this test if we oversubscribe. */
    for (long i = 0; i < iters; i++) {
        size_t n;
        if (i & 1) {
            n = rng_size(64) + 1;
        } else {
            n = rng_size(BIG_ALLOC) + 32768;   /* 32..288 KiB */
        }
        do_malloc(n);
        if (live_count >= MAX_LIVE / 2) {
            for (int k = 0; k < 8; k++) do_free_at(live_pick());
        }
        if ((i & 0x3f) == 0) verify_random_subset(8);
    }
    free_all();
}

static void scenario_realloc_chain(long iters) {
    (void)iters;
    /* Repeatedly grow a single allocation from 1 byte upward, then
     * shrink it.  Pattern catches realloc's in-place / merge / copy
     * paths. */
    void *p = do_malloc(1);
    if (!p) { fprintf(stderr, "  realloc_chain: initial malloc failed\n"); return; }
    int idx = live_count - 1;
    size_t sz = 1;
    size_t cap = MAX_ALLOC;  /* ~256 KiB ceiling */
    while (sz < cap) {
        sz = sz * 17 / 11 + 7;  /* not power of 2 */
        if (sz > cap) sz = cap;
        do_realloc_at(idx, sz);
    }
    while (sz > 1) {
        sz = sz / 3;
        if (sz < 1) sz = 1;
        do_realloc_at(idx, sz);
    }
    do_free_at(idx);
}

static void scenario_calloc_zero(long iters) {
    /* Verify calloc actually zeroes — every byte every time, no
     * stale-data leak. */
    for (long i = 0; i < iters; i++) {
        size_t n = rng_size(4096) + 1;
        /* dirty up the heap first */
        void *junk = malloc(n);
        if (junk) { memset(junk, 0xA5, n); free(junk); }
        void *p = calloc(1, n);
        if (!p) { stats.oom++; continue; }
        uint8_t *b = (uint8_t *)p;
        for (size_t j = 0; j < n; j++) {
            if (b[j] != 0) {
                fprintf(stderr,
                    "  calloc_zero: leak iter=%ld off=%zu got=0x%02x\n",
                    i, j, b[j]);
                stats.canary_failures++;
                break;
            }
        }
        free(p);
        stats.callocs++; stats.frees++;
    }
}

/* The "double_free" scenario relies on substrate's free() being
 * silent on bad-magic.  glibc / FreeBSD libc abort the process
 * (often with a useful diagnostic) — so this scenario is opt-in
 * by name and excluded from "all".  Same for free-on-stack-ptr. */
#if defined(__GNUC__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wuse-after-free"
#  pragma GCC diagnostic ignored "-Wfree-nonheap-object"
#endif
static void scenario_double_free(long iters) {
    free(NULL);
    free(NULL);

    char stack_buf[32];
    free((void *)stack_buf);    /* magic mismatch on substrate — no-op */

    for (long i = 0; i < iters; i++) {
        size_t n = rng_size(256) + 1;
        void *p = malloc(n);
        if (!p) continue;
        free(p);
        free(p);    /* magic now stale — substrate ignores */
        stats.double_frees++;
    }
}
#if defined(__GNUC__)
#  pragma GCC diagnostic pop
#endif

static void scenario_xterm_pattern(long iters) {
    /* Approximate Xfbdev / xterm allocation pattern: many small atoms
     * (40-120 bytes) interspersed with occasional medium pixmap-like
     * buffers (4-32 KiB) and rare big font buffers (~150 KiB —
     * cursor.builtin.gz inflates to roughly that). */
    for (long i = 0; i < iters; i++) {
        uint32_t op = rng_next() % 1000;
        if (op < 800) {
            do_malloc(40 + rng_size(80));
        } else if (op < 980) {
            do_malloc(4096 + rng_size(28 * 1024));
        } else {
            do_malloc(64 * 1024 + rng_size(96 * 1024));
        }
        if (live_count >= MAX_LIVE - 16) {
            for (int k = 0; k < 32; k++) do_free_at(live_pick());
        }
        if ((i & 0x1ff) == 0) verify_random_subset(16);
    }
    free_all();
}

static void scenario_fragmentation(long iters) {
    /* Alloc many, free every other, alloc more — exercises whether
     * coalesce + split correctly reuse holes vs growing the heap. */
    int N = (int)(iters > 200 ? 200 : iters);
    for (int i = 0; i < N; i++) do_malloc(200 + (size_t)(i & 31));
    /* free even slots */
    for (int i = 0; i < live_count; i += 2) {
        canary_check(live[i].ptr, live[i].size,
                     live[i].seed, live[i].alloc_id, "frag-pre");
        free(live[i].ptr);
        stats.frees++;
        live[i].ptr = NULL;
    }
    /* compact table */
    int w = 0;
    for (int r = 0; r < live_count; r++)
        if (live[r].ptr) live[w++] = live[r];
    stats.cur_bytes = 0;
    for (int i = 0; i < w; i++) stats.cur_bytes += (long)live[i].size;
    live_count = w;
    /* re-alloc same sizes — should reuse */
    for (int i = 0; i < N / 2; i++) do_malloc(200 + (size_t)(i & 31));
    verify_random_subset(64);
    free_all();
}

#if defined(__GNUC__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Warray-bounds"
#endif
static void scenario_zero_size(long iters) {
    /* substrate's malloc(0) returns a unique 1-byte allocation;
     * test passes the same ptr through free, doesn't crash.
     * (glibc may return a unique zero-bytes ptr — writing to it is
     *  UB on host, but we let -Warray-bounds slide since this whole
     *  scenario is *probing* allocator quirks.) */
    for (long i = 0; i < iters; i++) {
        void *p = malloc(0);
        if (!p) {
            fprintf(stderr, "  zero_size: malloc(0) returned NULL\n");
            stats.canary_failures++;
            continue;
        }
        /* Write the 1 byte we're promised on substrate. */
        *(volatile uint8_t *)p = 0xAB;
        free(p);
        stats.mallocs++; stats.frees++;
    }
    /* realloc(NULL, 0) — POSIX permits NULL or unique ptr. */
    void *p = realloc(NULL, 0);
    if (p) free(p);
    /* realloc(p, 0) — substrate frees and returns NULL. */
    p = malloc(16); stats.mallocs++;
    void *r = realloc(p, 0); stats.reallocs++;
    if (r != NULL) {
        free(r); stats.frees++;
    } else {
        stats.frees++;     /* realloc(,0) implicitly freed */
    }
}
#if defined(__GNUC__)
#  pragma GCC diagnostic pop
#endif

/* --- driver -------------------------------------------------------- */

struct scenario {
    const char *name;
    void (*fn)(long iters);
    long default_iters;
    int  in_all;   /* run under "all"? double_free / zero_size are
                      opt-in by name because they probe allocator
                      quirks that abort on host libcs */
};

static struct scenario scenarios[] = {
    { "sizes",         scenario_sizes,        1,             1 },
    { "storm",         scenario_storm,        100000,        1 },
    { "bigsmall",      scenario_bigsmall,     2000,          1 },
    { "realloc_chain", scenario_realloc_chain,1,             1 },
    { "calloc_zero",   scenario_calloc_zero,  20000,         1 },
    { "double_free",   scenario_double_free,  10000,         0 },
    { "xterm_pattern", scenario_xterm_pattern,50000,         1 },
    { "fragmentation", scenario_fragmentation,200,           1 },
    { "zero_size",     scenario_zero_size,    1000,          0 },
};

static void print_stats(const char *tag) {
    printf("--- stats: %s ---\n", tag);
    printf("  mallocs=%ld callocs=%ld reallocs=%ld frees=%ld\n",
           stats.mallocs, stats.callocs, stats.reallocs, stats.frees);
    printf("  canary_checks=%ld canary_failures=%ld\n",
           stats.canary_checks, stats.canary_failures);
    printf("  oom=%ld double_frees=%ld magic_violations=%ld\n",
           stats.oom, stats.double_frees, stats.magic_violations);
    printf("  peak_live=%ld peak_bytes=%ld cur_bytes=%ld live_count=%d\n",
           stats.peak_live, stats.peak_bytes, stats.cur_bytes, live_count);
}

static int run_scenario(const char *name, long iters) {
    int N = (int)(sizeof scenarios / sizeof scenarios[0]);
    for (int i = 0; i < N; i++) {
        if (strcmp(scenarios[i].name, name) != 0) continue;
        long n = iters > 0 ? iters : scenarios[i].default_iters;
        printf("==> scenario %s (iters=%ld)\n", name, n);
        long fails_before = stats.canary_failures;
        rng_seed(0xCAFEBABE ^ (uint32_t)i);
        scenarios[i].fn(n);
        long delta = stats.canary_failures - fails_before;
        printf("    -> %s (%ld canary fail%s)\n",
               delta ? "FAIL" : "PASS", delta, delta == 1 ? "" : "s");
        return delta == 0 ? 0 : 1;
    }
    fprintf(stderr, "torture_malloc: unknown scenario '%s'\n", name);
    return 2;
}

int main(int argc, char **argv) {
    /* Substrate's kernel `initarg='x y'` passes the whole quoted
     * string as a single argv[1].  Detect an embedded space and
     * split it so `initarg='storm 50000'` works the same as
     * `torture_malloc storm 50000`. */
    char split_buf[64] = {0};
    char *which_dyn = NULL;
    char *iters_str = NULL;
    if (argc == 2 && strchr(argv[1], ' ')) {
        size_t n = strlen(argv[1]);
        if (n < sizeof(split_buf)) {
            memcpy(split_buf, argv[1], n + 1);
            which_dyn = split_buf;
            char *sp = strchr(split_buf, ' ');
            *sp++ = '\0';
            iters_str = sp;
        }
    }
    const char *which = which_dyn ? which_dyn : (argc > 1 ? argv[1] : "all");
    long iters = iters_str ? atol(iters_str)
                           : (argc > 2 ? atol(argv[2]) : 0);

    if (getenv("TORTURE_VERBOSE")) verbose = 1;

    printf("torture_malloc: starting\n");
    printf("  build: %s\n",
#ifdef __substrate__
        "substrate"
#else
        "host"
#endif
    );
    printf("  MAX_LIVE=%d MAX_ALLOC=%d BIG_ALLOC=%d\n",
           MAX_LIVE, MAX_ALLOC, BIG_ALLOC);

    int rc = 0;
    if (strcmp(which, "all") == 0) {
        int N = (int)(sizeof scenarios / sizeof scenarios[0]);
        for (int i = 0; i < N; i++) {
            if (!scenarios[i].in_all) continue;
            rc |= run_scenario(scenarios[i].name, 0);
        }
    } else {
        rc = run_scenario(which, iters);
    }

    print_stats("end-of-run");
    if (rc == 0)
        printf("torture_malloc: ALL PASS\n");
    else
        printf("torture_malloc: FAIL (rc=%d)\n", rc);
    return rc;
}
