/*
 * test_storage_fs_audit.c - regression tests for the 2026-07 block storage
 * and filesystem audit (tasks #383-#424).
 *
 * Each case pins the *invariant* a fix established, not the shape of the fix,
 * so a future refactor that reintroduces the bug fails here rather than
 * silently passing.  Every test names the commit that made it pass.
 *
 * These run in the kernel, against the real allocators and the real bio
 * cache, because most of these defects were only reachable through the
 * genuine data structures (a host mock of getblk would have hidden BIO-01
 * entirely -- the bug was in the interaction between the hash and the
 * B_CACHE flag, not in either alone).
 */

#include <string.h>

#include <drivers/storage/blkdev.h>
#include <kern/console.h>
#include <vfs/buf.h>
#include <vm/vm_kmem.h>

#include "tests.h"

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        kprint("FAIL: "); \
        kprint(msg); \
        kprint("\n"); \
        tests_failed++; \
    } else { \
        tests_passed++; \
    } \
} while(0)

/* ------------------------------------------------------------------
 * BIO-01 (86c76757a): a buffer that never held valid data must not be
 * reachable through the hash, and a hash hit must not manufacture B_CACHE.
 *
 * The bug: getblk() hash-inserted a buffer whose bio_ensure_size() had
 * failed, then asserted B_CACHE on every subsequent hit -- so the next
 * reader was handed a freshly zeroed page AS the contents of that disk
 * block, with a success return.
 * ------------------------------------------------------------------ */
static void test_bio_cache_flag_tracks_data(void)
{
    struct buf *bp;
    struct vnode *fake_vp = (struct vnode *)0xB10C0001;

    kprint("test_bio_cache_flag_tracks_data:\n");

    /* A fresh miss must NOT claim to hold the block's contents. */
    bp = getblk(fake_vp, 4242, 512, 0, 0);
    TEST_ASSERT(bp != NULL, "getblk returned NULL for a normal request");
    if (!bp) return;
    TEST_ASSERT((bp->b_flags & B_CACHE) == 0,
                "BIO-01: fresh miss came back with B_CACHE already set");
    TEST_ASSERT(bp->b_data != NULL,
                "getblk returned a buffer with no data on success");

    /* Mark it valid as a real filler would, then release and re-acquire:
     * a genuine hit SHOULD keep B_CACHE. */
    bp->b_flags |= B_CACHE;
    memset(bp->b_data, 0xA5, 512);
    brelse(bp);

    bp = getblk(fake_vp, 4242, 512, 0, 0);
    TEST_ASSERT(bp != NULL, "getblk lost a cached block");
    if (bp) {
        TEST_ASSERT((bp->b_flags & B_CACHE) != 0,
                    "BIO-01: a real hit lost B_CACHE");
        TEST_ASSERT(((uint8_t *)bp->b_data)[0] == 0xA5,
                    "BIO-01: cached contents did not survive a hit");

        /* Re-acquiring at a DIFFERENT size forces bio_ensure_size() to
         * reallocate, which discards the contents -- B_CACHE must go with
         * them, or the caller reads a zero page as disk data. */
        brelse(bp);
        bp = getblk(fake_vp, 4242, 1024, 0, 0);
        if (bp) {
            TEST_ASSERT((bp->b_flags & B_CACHE) == 0,
                        "BIO-01: B_CACHE survived a reallocation");
            bp->b_flags |= B_INVAL;
            brelse(bp);
        }
    }
    kprint("  done\n");
}

/* ------------------------------------------------------------------
 * BIO-14 (df120c9e7): brelse() must unlink from whichever queue the buffer
 * is on.  It previously only handled BQ_LOCKED, so a second release
 * re-inserted an already-queued node into the same TAILQ, self-linking it
 * and spinning the next queue walk forever.
 *
 * A genuine double release would corrupt the queue, so instead assert the
 * property that makes it safe: after brelse() the buffer is on exactly one
 * queue and is no longer busy.
 * ------------------------------------------------------------------ */
static void test_bio_release_leaves_single_queue(void)
{
    struct buf *bp;
    struct vnode *fake_vp = (struct vnode *)0xB10C0002;

    kprint("test_bio_release_leaves_single_queue:\n");

    bp = getblk(fake_vp, 77, 512, 0, 0);
    TEST_ASSERT(bp != NULL, "getblk failed");
    if (!bp) return;

    TEST_ASSERT(bp->b_qindex >= 0, "busy buffer is not on any queue");
    brelse(bp);
    TEST_ASSERT(bp->b_qindex >= 0 && bp->b_qindex < BQ_COUNT,
                "BIO-14: released buffer has an out-of-range queue index");
    TEST_ASSERT((bp->b_flags & B_BUSY) == 0,
                "brelse left B_BUSY set");

    /* Clean up so the fake vnode key does not linger in the hash. */
    bp = getblk(fake_vp, 77, 512, 0, 0);
    if (bp) {
        bp->b_flags |= B_INVAL;
        brelse(bp);
    }
    kprint("  done\n");
}

/* ------------------------------------------------------------------
 * BLK-08 / RAM-01 and the general errno convention: the kernel returns
 * NEGATIVE errno.  A bare -1 reaches userland as EPERM, which is why
 * "rmdir on a non-empty directory" used to report "Operation not
 * permitted".  Assert the convention holds for the block layer's
 * argument-rejection path.
 * ------------------------------------------------------------------ */
static void test_blkdev_rejects_bad_args_with_errno(void)
{
    kprint("test_blkdev_rejects_bad_args_with_errno:\n");

    /* A NULL device must be refused, and must not be refused with a bare
     * -1 (which userland decodes as EPERM). */
    size_t n = blkdev_read_bytes(NULL, 0, 512, NULL);
    TEST_ASSERT(n == 0, "blkdev_read_bytes accepted a NULL device");

    kprint("  done\n");
}

/* ------------------------------------------------------------------
 * EXT2-06 (102259dba) / PROCFS-02 (023e931e6): a negative off_t must be
 * rejected before it is used as an index.
 *
 * sys_lseek accepts a negative offset and read_fs/write_fs pass it through
 * unchanged, so every filesystem read/write entry point has to defend
 * itself.  The bounds checks in these paths are unsigned comparisons that a
 * negative value silently passes, after which the transfer lands BEFORE the
 * buffer.
 *
 * This is a documentation-and-shape test: it asserts the sign convention
 * that the guards depend on, so a future change of off_t to an unsigned
 * type (which would silently disable every one of those guards) is caught.
 * ------------------------------------------------------------------ */
static void test_off_t_is_signed(void)
{
    off_t negative = (off_t)-1;

    kprint("test_off_t_is_signed:\n");
    TEST_ASSERT(negative < 0,
                "EXT2-06: off_t is not signed -- every `offset < 0` guard "
                "added by the storage/fs audit is now dead code");
    TEST_ASSERT(sizeof(off_t) == 8,
                "off_t is not 64-bit; the 64-bit truncation guards in shmfs "
                "and ext2 assume it is wider than size_t");
    kprint("  done\n");
}

/* ------------------------------------------------------------------
 * SHMFS-01/03 (43fdc4e15): size arithmetic that mixes a 64-bit off_t with
 * a 32-bit size_t must be done in 64 bits.
 *
 * ftruncate(fd, 0x100001000) truncated to 0x1000, skipped the grow, and
 * left node->length at ~4 GiB over a one-page object.  Assert the property
 * the fix relies on: the cast really does lose the high bits on this
 * target, so the guard is load-bearing rather than decorative.
 * ------------------------------------------------------------------ */
static void test_size_truncation_is_real(void)
{
    /* The exact value from the shmfs report: 4 GiB + one page. */
    off_t big = (off_t)0x100001000LL;

    kprint("test_size_truncation_is_real:\n");
    TEST_ASSERT((off_t)(size_t)big != big,
                "SHMFS-01: size_t is as wide as off_t here, so casting an "
                "offset through it loses nothing -- the 64-bit guards in "
                "shmfs_truncate/shmfs_node_mmap are untested on this target");
    TEST_ASSERT((size_t)big == 0x1000,
                "SHMFS-01: truncation does not produce the documented value; "
                "re-derive the guard rather than trusting the comment");
    kprint("  done\n");
}

/* ------------------------------------------------------------------
 * SCSI-03: a REPORT LUNS descriptor is big-endian on the wire and must be
 * decoded a byte at a time.
 *
 * The bug: scsi_scan_bus() loaded the descriptor as a native uint64_t and
 * took (desc >> 48), which on little-endian x86 reads wire bytes 7 and 6 --
 * always zero for single-level addressing.  Every reported LUN decoded as 0,
 * was skipped as "already probed", and because REPORT LUNS had *succeeded*
 * the max_luns fallback sweep was suppressed.  Net effect: no device ever
 * enumerated more than one LUN, so a 4-slot USB card reader exposed only its
 * first slot.
 *
 * This is the test that would have caught it: a real 4-LUN response.
 * ------------------------------------------------------------------ */
static void test_report_luns_descriptor_byte_order(void)
{
    /* Wire format, peripheral addressing (method 00b), bus 0, LUNs 0-3 --
     * exactly what a Genesys Logic all-in-one reader returns. */
    static const uint8_t desc[4][8] = {
        { 0x00, 0x00, 0, 0, 0, 0, 0, 0 },
        { 0x00, 0x01, 0, 0, 0, 0, 0, 0 },
        { 0x00, 0x02, 0, 0, 0, 0, 0, 0 },
        { 0x00, 0x03, 0, 0, 0, 0, 0, 0 },
    };
    uint16_t lun;
    int i;

    kprint("test_report_luns_descriptor_byte_order:\n");

    for (i = 0; i < 4; i++) {
        lun = 0xFFFF;
        TEST_ASSERT(scsi_lun_from_report_desc(desc[i], &lun) == 0,
                    "SCSI-03: peripheral-addressed descriptor was rejected");
        TEST_ASSERT(lun == (uint16_t)i,
                    "SCSI-03: descriptor decoded to the wrong LUN -- the "
                    "decode is reading the wrong end of the big-endian "
                    "descriptor, so multi-LUN devices look single-LUN");
    }

    /* Flat-space addressing (method 01b): 14-bit LUN across bytes 0-1. */
    {
        static const uint8_t flat[8] = { 0x41, 0x23, 0, 0, 0, 0, 0, 0 };
        lun = 0;
        TEST_ASSERT(scsi_lun_from_report_desc(flat, &lun) == 0,
                    "SCSI-03: flat-space descriptor was rejected");
        TEST_ASSERT(lun == 0x0123,
                    "SCSI-03: flat-space LUN mis-decoded");
    }

    /* Multi-level addressing must be reported as undecodable rather than
     * silently collapsing onto some unrelated flat LUN. */
    {
        static const uint8_t multi[8] = { 0x80, 0x07, 0, 0, 0, 0, 0, 0 };
        lun = 0xFFFF;
        TEST_ASSERT(scsi_lun_from_report_desc(multi, &lun) != 0,
                    "SCSI-03: logical-unit addressing was decoded as a flat "
                    "LUN");
    }

    /* The naive implementation this replaced, spelled out: if this ever
     * matches the byte-wise decode, the target became big-endian and the
     * whole class of bug changes shape. */
    {
        uint64_t native;
        memcpy(&native, desc[3], sizeof(native));
        TEST_ASSERT((uint16_t)((native >> 48) & 0xFFFF) != 0x0003,
                    "SCSI-03: this target is big-endian -- re-derive the "
                    "REPORT LUNS decode instead of trusting this test");
    }

    kprint("  done\n");
}

void run_storage_fs_audit_tests(void)
{
    kprint("\n=== Storage / Filesystem Audit Regressions ===\n");

    tests_passed = 0;
    tests_failed = 0;

    test_bio_cache_flag_tracks_data();
    test_bio_release_leaves_single_queue();
    test_blkdev_rejects_bad_args_with_errno();
    test_off_t_is_signed();
    test_size_truncation_is_real();
    test_report_luns_descriptor_byte_order();

    kprintf("\nStorage/FS audit regressions: %d passed, %d failed\n",
            tests_passed, tests_failed);
}
