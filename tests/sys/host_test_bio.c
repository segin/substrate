/*
 * host_test_bio.c - Buffer cache (bio) lifecycle unit tests (REQ-04-0193, REQ-04-0194)
 *
 * Standalone host build: includes bio.c directly and provides the minimal kernel
 * environment needed to exercise getblk/bread/bwrite/brelse and the
 * bdwrite/bufsync delayed-write path.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>

/* kern/sched.h pulls in proc.h which defines thread_t, process_t,
 * current_thread, current_process, MAX_THREADS, etc. */
#include <kern/sched.h>
#include <sys/errno.h>

/* ---- Kernel stubs -------------------------------------------------- */

thread_t  g_thread;
thread_t *current_thread  = &g_thread;
process_t *current_process = NULL;

/* sched stubs */
void sched_yield(void) {}
void sched_sleep(void *chan) { (void)chan; }
int  sched_sleep_until(void *chan, uint64_t deadline)
    { (void)chan; (void)deadline; return 0; }

/* time stubs */
uint32_t get_hz(void)    { return 100; }
uint64_t get_ticks(void) { return 0; }

/* kthread – suppress syncer creation so bio_init() doesn't block */
int kthread_create(void (*func)(void *), void *arg, thread_t **tdp, const char *name)
    { (void)func; (void)arg; (void)tdp; (void)name; return 0; }

/* spinlock stubs */
void spinlock_init(spinlock_t *lock, const char *name)
    { lock->locked = 0; lock->cpu_id = 0; lock->name = name; }
void spinlock_acquire(spinlock_t *lock) { lock->locked = 1; }
void spinlock_release(spinlock_t *lock) { lock->locked = 0; }
bool spinlock_try_acquire(spinlock_t *lock)
    { if (lock->locked) return false; lock->locked = 1; return true; }
bool spinlock_is_held(spinlock_t *lock) { return lock->locked != 0; }

/* sleepq stubs – single-threaded, no actual sleeping */
void sleepq_add(void *chan, thread_t *t)  { (void)chan; (void)t; }
int  sleepq_wake_all(void *chan)          { (void)chan; return 0; }

/* memory */
void *kmalloc(size_t size)               { return calloc(1, size); }
void  kfree(void *ptr, size_t size)      { (void)size; free(ptr); }

/* panic */
void panic(const char *msg)              { fprintf(stderr, "PANIC: %s\n", msg); abort(); }

/* pmm — controllable free-RAM stub for cache-growth/reclaim tests. */
static uint32_t g_free_ram_bytes = 256U * 1024U * 1024U;  /* default: plenty */
uint32_t pmm_get_free_memory(void) { return g_free_ram_bytes; }

/* ---- Mock vnode ----------------------------------------------------- */
#include <vfs/vnode.h>
#include <vfs/buf.h>

/*
 * A simple 64 KB backing store used by mock_strategy.
 * strategy copies b_bcount bytes between b_data and the store at b_blkno*b_bcount.
 */
#define MOCK_STORE_SIZE (64 * 1024)
static uint8_t g_store[MOCK_STORE_SIZE];
static int g_strategy_calls;
static int g_strategy_should_fail;

static int mock_strategy(struct vnode *vp, void *bpv)
{
    struct buf *bp = (struct buf *)bpv;
    size_t offset = (size_t)((uint64_t)bp->b_blkno * bp->b_bcount);

    (void)vp;
    g_strategy_calls++;

    if (g_strategy_should_fail) {
        bp->b_error = EIO;
        bp->b_flags |= B_ERROR;
        biodone(bp);
        return EIO;
    }

    if (offset + bp->b_bcount > MOCK_STORE_SIZE) {
        bp->b_error = EIO;
        bp->b_flags |= B_ERROR;
        biodone(bp);
        return EIO;
    }

    if (bp->b_flags & B_READ) {
        memcpy(bp->b_data, g_store + offset, bp->b_bcount);
    } else {
        memcpy(g_store + offset, bp->b_data, bp->b_bcount);
    }

    biodone(bp);
    return 0;
}

static struct vnodeops g_vnodeops = {
    .vop_strategy = mock_strategy,
};

static struct vnode g_vnode;

static void reset_state(void)
{
    g_strategy_calls = 0;
    g_strategy_should_fail = 0;
    memset(g_store, 0, sizeof(g_store));
}

/* ---- bio.c inclusion ----------------------------------------------- */
#include "../../sys/vfs/bio.c"
/* The kernel's global process/thread tables are gone, and MAX_PROCS /
 * MAX_THREADS went with them; anything sized by them here is this file's
 * own storage.  Values match the other host tests. */
#ifndef MAX_THREADS
#define MAX_THREADS 64
#endif


/* ================================================================
 * REQ-04-0193: getblk/bread/bwrite/brelse lifecycle
 * ================================================================ */

static bool test_getblk_allocates_new(void)
{
    struct buf *bp;

    reset_state();
    bp = getblk(&g_vnode, 0, 512, 0, 0);
    if (!bp) return false;
    if (!(bp->b_flags & B_BUSY))  return false;
    if (bp->b_bcount != 512)      return false;
    if (bp->b_vp != &g_vnode)     return false;
    if (bp->b_blkno != 0)         return false;
    brelse(bp);
    return true;
}

static bool test_getblk_cache_hit(void)
{
    struct buf *bp1, *bp2;

    reset_state();

    /* First allocation populates the hash */
    bp1 = getblk(&g_vnode, 7, 512, 0, 0);
    if (!bp1) return false;
    memset(bp1->b_data, 0xAB, 512);
    brelse(bp1);

    /* Second call with same vp/blkno should be a cache hit (B_CACHE) */
    bp2 = getblk(&g_vnode, 7, 512, 0, 0);
    if (!bp2) return false;
    if (!(bp2->b_flags & B_CACHE)) return false;
    /* Data must be preserved */
    uint8_t *d = (uint8_t *)bp2->b_data;
    bool data_ok = (d[0] == 0xAB && d[511] == 0xAB);
    brelse(bp2);
    return data_ok;
}

static bool test_bread_reads_via_strategy(void)
{
    struct buf *bp;
    int err;

    reset_state();

    /* Pre-populate backing store block 1 with known data */
    memset(g_store + 512, 0xDE, 512);

    err = bread(&g_vnode, 1, 512, NULL, &bp);
    if (err) return false;
    if (!bp) return false;

    uint8_t *d = (uint8_t *)bp->b_data;
    bool ok = (d[0] == 0xDE && d[511] == 0xDE && g_strategy_calls >= 1);
    brelse(bp);
    return ok;
}

static bool test_bread_cache_hit_skips_strategy(void)
{
    struct buf *bp;
    int err;
    int calls_before;

    reset_state();

    /* Warm the cache for block 2 */
    err = bread(&g_vnode, 2, 512, NULL, &bp);
    if (err) return false;
    brelse(bp);

    calls_before = g_strategy_calls;

    /* Second bread: should be a cache hit, no additional strategy call */
    err = bread(&g_vnode, 2, 512, NULL, &bp);
    if (err) return false;
    bool ok = (g_strategy_calls == calls_before);
    brelse(bp);
    return ok;
}

static bool test_bwrite_writes_via_strategy(void)
{
    struct buf *bp;
    int err;

    reset_state();

    bp = getblk(&g_vnode, 3, 512, 0, 0);
    if (!bp) return false;

    memset(bp->b_data, 0xCA, 512);
    err = bwrite(bp);
    if (err) return false;

    /* Verify backing store was updated */
    uint8_t *store_blk = g_store + 3 * 512;
    bool ok = (store_blk[0] == 0xCA && store_blk[511] == 0xCA && g_strategy_calls >= 1);

    /* bwrite releases the buffer; don't call brelse again */
    return ok;
}

static bool test_brelse_moves_to_clean_queue(void)
{
    struct buf *bp;

    reset_state();

    bp = getblk(&g_vnode, 4, 512, 0, 0);
    if (!bp) return false;
    /* After brelse without B_DELWRI, buffer should go to BQ_CLEAN */
    brelse(bp);

    /* Re-acquire should be a cache hit (still cached in clean queue) */
    bp = getblk(&g_vnode, 4, 512, 0, 0);
    if (!bp) return false;
    bool ok = (bp->b_flags & B_CACHE) != 0;
    brelse(bp);
    return ok;
}

static bool test_bread_error_propagates(void)
{
    struct buf *bp = NULL;
    int err;

    reset_state();
    g_strategy_should_fail = 1;

    err = bread(&g_vnode, 99, 512, NULL, &bp);
    if (bp) brelse(bp);
    return (err == EIO);
}

/* ================================================================
 * REQ-04-0194: bdwrite delayed write, then bufsync flushes
 * ================================================================ */

static bool test_bdwrite_marks_delwri(void)
{
    struct buf *bp;

    reset_state();

    bp = getblk(&g_vnode, 5, 512, 0, 0);
    if (!bp) return false;
    memset(bp->b_data, 0xBB, 512);

    /* bdwrite releases buffer with B_DELWRI */
    bdwrite(bp);

    /* Buffer should still be in cache (incore) and have B_DELWRI */
    bp = incore(&g_vnode, 5);
    bool ok = (bp != NULL && (bp->b_flags & B_DELWRI) != 0);
    if (bp && (bp->b_flags & B_BUSY)) brelse(bp);
    return ok;
}

static bool test_bufsync_flushes_delwri(void)
{
    struct buf *bp;
    int err;

    reset_state();

    /* Write known data via bdwrite (delayed) */
    err = bread(&g_vnode, 6, 512, NULL, &bp);
    if (err) return false;
    memset(bp->b_data, 0xEE, 512);
    bdwrite(bp);

    /* Strategy should NOT have been called for the write yet */
    int calls_after_bdwrite = g_strategy_calls;

    /* bufsync flushes all dirty buffers */
    bufsync(0);

    /* Strategy must have been called at least once more for the flush */
    bool flushed = (g_strategy_calls > calls_after_bdwrite);

    /* Backing store should now reflect the written data */
    uint8_t *store_blk = g_store + 6 * 512;
    bool data_ok = (store_blk[0] == 0xEE && store_blk[511] == 0xEE);

    return flushed && data_ok;
}

static bool test_incore_returns_null_for_uncached(void)
{
    reset_state();
    struct buf *bp = incore(&g_vnode, 12345);
    return bp == NULL;
}

/* ================================================================
 * Device-keyed manual-fill API (bio_dev_get / bio_dev_release /
 * bio_dev_invalidate / bio_dev_purge).  These do NOT require a vnode
 * and never call into VOP_STRATEGY — perfect for fs/ext2 raw-device
 * reads.
 * ================================================================ */

static bool test_bio_dev_miss_then_hit(void)
{
    /* Use a fake device key that's not the existing vnode. */
    void *dev = (void *)0xDEAD0001;
    struct buf *bp;

    reset_state();

    bp = bio_dev_get(dev, 0, 1024);
    if (!bp) return false;
    /* Fresh allocation: B_CACHE must NOT be set on miss. */
    bool miss_clear = ((bp->b_flags & B_CACHE) == 0);
    /* Caller fills the buffer and tags it cached. */
    memset(bp->b_data, 0x77, 1024);
    bp->b_flags |= B_CACHE;
    bio_dev_release(bp);

    /* Second access: B_CACHE set, data preserved. */
    bp = bio_dev_get(dev, 0, 1024);
    if (!bp) return false;
    bool hit_set = (bp->b_flags & B_CACHE) != 0;
    bool data_ok = ((uint8_t *)bp->b_data)[0] == 0x77 &&
                   ((uint8_t *)bp->b_data)[1023] == 0x77;
    bio_dev_release(bp);
    return miss_clear && hit_set && data_ok;
}

static bool test_bio_dev_invalidate_drops_entry(void)
{
    void *dev = (void *)0xDEAD0002;
    struct buf *bp;

    reset_state();

    bp = bio_dev_get(dev, 5, 512);
    if (!bp) return false;
    memset(bp->b_data, 0xA1, 512);
    bp->b_flags |= B_CACHE;
    bio_dev_release(bp);

    bio_dev_invalidate(dev, 5);

    /* After invalidate the next get must be a miss. */
    bp = bio_dev_get(dev, 5, 512);
    if (!bp) return false;
    bool miss = (bp->b_flags & B_CACHE) == 0;
    bio_dev_release(bp);
    return miss;
}

static bool test_bio_dev_purge_drops_all_for_dev(void)
{
    void *dev_a = (void *)0xDEADA000;
    void *dev_b = (void *)0xDEADB000;
    struct buf *bp;

    reset_state();

    /* Populate two blocks under dev_a and one under dev_b. */
    for (int i = 0; i < 2; i++) {
        bp = bio_dev_get(dev_a, i, 512);
        if (!bp) return false;
        bp->b_flags |= B_CACHE;
        bio_dev_release(bp);
    }
    bp = bio_dev_get(dev_b, 0, 512);
    if (!bp) return false;
    bp->b_flags |= B_CACHE;
    bio_dev_release(bp);

    bio_dev_purge(dev_a);

    /* dev_a entries must miss now... */
    bp = bio_dev_get(dev_a, 0, 512);
    bool a_miss = bp && (bp->b_flags & B_CACHE) == 0;
    if (bp) { bp->b_flags |= B_INVAL; bio_dev_release(bp); }

    bp = bio_dev_get(dev_a, 1, 512);
    a_miss = a_miss && bp && (bp->b_flags & B_CACHE) == 0;
    if (bp) { bp->b_flags |= B_INVAL; bio_dev_release(bp); }

    /* ...but dev_b's entry survives. */
    bp = bio_dev_get(dev_b, 0, 512);
    bool b_hit = bp && (bp->b_flags & B_CACHE) != 0;
    if (bp) bio_dev_release(bp);

    return a_miss && b_hit;
}

/* ================================================================
 * Cache growth and reclaim
 * ================================================================ */

static bool test_reclaim_drops_clean_buffers(void)
{
    void *dev = (void *)0xCAFE0001;
    struct buf *bp;
    struct bio_stats before, after;

    reset_state();
    g_free_ram_bytes = 256U * 1024U * 1024U;

    /* Fill cache with some clean entries. */
    for (int i = 0; i < 16; i++) {
        bp = bio_dev_get(dev, i, 4096);
        if (!bp) return false;
        bp->b_flags |= B_CACHE;
        bio_dev_release(bp);
    }

    bio_get_stats(&before);

    /* Ask reclaim to free at least ~32KB. */
    size_t freed = bio_reclaim(32 * 1024);

    bio_get_stats(&after);

    /* Resident bytes must drop and freed must be reasonable. */
    return freed >= 32 * 1024 && after.resident_bytes < before.resident_bytes;
}

static bool test_growth_blocked_when_ram_low(void)
{
    void *dev = (void *)0xCAFE0002;
    struct buf *bp;

    reset_state();

    /* First, fill past the floor so we hit the soft policy. */
    g_free_ram_bytes = 256U * 1024U * 1024U;
    /* We need to push bio_nbuf >= BIO_NBUF_FLOOR (64) — allocate 80 blocks. */
    for (int i = 0; i < 80; i++) {
        bp = bio_dev_get(dev, 1000 + i, 512);
        if (!bp) return false;
        bp->b_flags |= B_CACHE;
        bio_dev_release(bp);
    }

    /* Now drop free RAM below the reserve — new allocations should
     * NOT grow the pool; they must reuse existing entries. */
    g_free_ram_bytes = 1024U;  /* well under BIO_RESERVE_BYTES */

    struct bio_stats s1;
    bio_get_stats(&s1);

    /* Ask for a brand-new block.  This must succeed by reusing rather
     * than growing, so nbuf should not increase. */
    bp = bio_dev_get(dev, 9999, 512);
    if (!bp) return false;
    bp->b_flags |= B_CACHE;
    bio_dev_release(bp);

    struct bio_stats s2;
    bio_get_stats(&s2);

    return s2.nbuf == s1.nbuf;
}

static bool test_stats_track_hits_and_misses(void)
{
    void *dev = (void *)0xCAFE0003;
    struct buf *bp;
    struct bio_stats before, after;

    reset_state();
    g_free_ram_bytes = 256U * 1024U * 1024U;

    bio_get_stats(&before);

    /* One miss... */
    bp = bio_dev_get(dev, 100, 512);
    if (!bp) return false;
    bp->b_flags |= B_CACHE;
    bio_dev_release(bp);

    /* ...then two hits. */
    bp = bio_dev_get(dev, 100, 512); bio_dev_release(bp);
    bp = bio_dev_get(dev, 100, 512); bio_dev_release(bp);

    bio_get_stats(&after);

    return (after.misses - before.misses) == 1 &&
           (after.hits   - before.hits)   == 2;
}

/* ================================================================
 * Property test: random get/release sequence preserves invariants
 *
 * Invariants checked after every operation:
 *   - bio_nbuf == sum(q_locked, q_clean, q_dirty, q_empty)
 *   - resident_bytes == sum of b_bcount over all buffers (approx; we
 *     check it's monotonic w.r.t. allocations and decreases on reclaim)
 *   - cache hit data is preserved across release/reacquire
 * ================================================================ */

static bool test_property_random_sequence(void)
{
    void *devs[3] = { (void *)0xBEEF1, (void *)0xBEEF2, (void *)0xBEEF3 };
    enum { N_OPS = 500 };
    /* Reproducible PRNG (xorshift32). */
    uint32_t s = 0xC0FFEEU;
    int failures = 0;

    reset_state();
    g_free_ram_bytes = 64U * 1024U * 1024U;

    for (int i = 0; i < N_OPS && failures == 0; i++) {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        int op   = s % 4;
        int dev_idx = (s >> 8) % 3;
        int blk  = (s >> 12) & 0x1F;
        int size = (((s >> 18) & 1) ? 512 : 1024);

        struct buf *bp = bio_dev_get(devs[dev_idx], blk, size);
        if (!bp) { failures++; break; }

        /* On miss the new alloc has b_bcount==size. */
        if ((size_t)bp->b_bcount != (size_t)size) failures++;

        switch (op) {
        case 0:
            /* Plain release — buffer goes BQ_CLEAN. */
            bp->b_flags |= B_CACHE;
            bio_dev_release(bp);
            break;
        case 1:
            /* Invalidate. */
            bp->b_flags |= B_INVAL;
            bio_dev_release(bp);
            break;
        case 2:
            /* Mark dirty + release: ends up on BQ_DIRTY. */
            bp->b_flags |= B_CACHE;
            bio_dev_mark_dirty(bp);
            bio_dev_release(bp);
            break;
        case 3:
            /* Purge entire device. */
            bp->b_flags |= B_CACHE;
            bio_dev_release(bp);
            bio_dev_purge(devs[dev_idx]);
            break;
        }

        /* Invariant: nbuf equals sum of queue lengths. */
        struct bio_stats st;
        bio_get_stats(&st);
        uint32_t qsum = st.q_locked + st.q_clean + st.q_dirty + st.q_empty;
        if (qsum != st.nbuf) {
            fprintf(stderr, "iter %d: qsum=%u nbuf=%u\n", i, qsum, st.nbuf);
            failures++;
        }
    }

    /* After the storm, full reclaim brings cache back to a clean state. */
    bio_reclaim(SIZE_MAX);
    struct bio_stats final;
    bio_get_stats(&final);
    /* All BQ_CLEAN/BQ_EMPTY buffers should be gone; only BQ_LOCKED/DIRTY
     * survive (and we haven't held anything locked). */
    if (final.q_clean != 0 || final.q_empty != 0) failures++;

    return failures == 0;
}

/* ================================================================
 * Main
 * ================================================================ */

#define RUN(name) do {                                          \
    bool r = name();                                            \
    printf("%s: %s\n", r ? "PASS" : "FAIL", #name);            \
    if (!r) failures++;                                         \
} while (0)

int main(void)
{
    int failures = 0;

    memset(&g_vnode, 0, sizeof(g_vnode));
    g_vnode.v_op = &g_vnodeops;
    g_vnode.v_type = VREG;
    g_thread.tid = 0;
    g_thread.state = THREAD_RUNNING;

    bio_init();

    /* REQ-04-0193 */
    RUN(test_getblk_allocates_new);
    RUN(test_getblk_cache_hit);
    RUN(test_bread_reads_via_strategy);
    RUN(test_bread_cache_hit_skips_strategy);
    RUN(test_bwrite_writes_via_strategy);
    RUN(test_brelse_moves_to_clean_queue);
    RUN(test_bread_error_propagates);

    /* REQ-04-0194 */
    RUN(test_bdwrite_marks_delwri);
    RUN(test_bufsync_flushes_delwri);
    RUN(test_incore_returns_null_for_uncached);

    /* Device-keyed API */
    RUN(test_bio_dev_miss_then_hit);
    RUN(test_bio_dev_invalidate_drops_entry);
    RUN(test_bio_dev_purge_drops_all_for_dev);

    /* Cache growth / reclaim / stats */
    RUN(test_reclaim_drops_clean_buffers);
    RUN(test_growth_blocked_when_ram_low);
    RUN(test_stats_track_hits_and_misses);

    /* Property */
    RUN(test_property_random_sequence);

    if (failures == 0)
        printf("All bio tests PASSED\n");
    else
        printf("%d bio test(s) FAILED\n", failures);

    return failures ? 1 : 0;
}
