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

    if (failures == 0)
        printf("All bio tests PASSED\n");
    else
        printf("%d bio test(s) FAILED\n", failures);

    return failures ? 1 : 0;
}
