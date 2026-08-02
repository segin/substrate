#ifndef _BLKDEV_H
#define _BLKDEV_H

#include <stdint.h>
#include <stddef.h>
#include <vfs/vfs.h>

struct geom_disk;

// Block device structure
typedef struct blkdev {
    char name[32];              // Device name (e.g., "ide0", "ram0")
    uint32_t sector_size;       // Typically 512
    uint64_t total_sectors;     // Total sectors on device
    void *priv;                 // Driver-private data
    
    // Driver callbacks
    int (*read)(struct blkdev *dev, uint64_t sector, uint32_t count, void *buffer);
    int (*write)(struct blkdev *dev, uint64_t sector, uint32_t count, const void *buffer);
    int (*ioctl)(struct blkdev *dev, uint32_t request, void *arg);

    int dead;                   // 1 once removed/unplugged: I/O returns -EIO

    /* Sequential read-ahead heuristic (blkdev.c).  ra_next is the sector where
     * the next sequential read is expected; a read starting there grows
     * ra_window and prefetches that many sectors ahead into the bio cache, so
     * a streaming reader's subsequent reads hit cache instead of a fresh
     * device round-trip.  ra_next/ra_window are hints, updated lock-free: a
     * race between concurrent readers costs at most a wasted prefetch, never
     * correctness (prefetched data is always read straight from the device).
     * ra_buf is a per-device scratch buffer (BLKDEV_RA_MAX sectors) allocated
     * lazily on the first prefetch so a sustained stream never re-allocates;
     * ra_busy serialises prefetches on it (a reader finding it set just skips
     * its prefetch). */
    uint64_t ra_next;           // expected next sequential sector
    uint32_t ra_window;         // current read-ahead window (sectors)
    void    *ra_buf;            // lazily-allocated read-ahead scratch
    volatile int ra_busy;       // 1 while a prefetch owns ra_buf
    /* BLK-07: the sector size ra_buf was sized for.  A device whose sector
     * size changes (ATAPI 512 <-> 2048 on media change) must re-allocate, or
     * the buffer overflows and its kfree is mis-sized. */
    uint32_t ra_ss;
    /*
     * BLK-03: for a PARTITION blkdev, the raw disk it lives on and the
     * absolute sector its sector 0 maps to.  A write through a partition
     * node has to invalidate the raw device's cache of the same physical
     * sectors; without this back-pointer only the raw->partition direction
     * was covered, so raw reads kept serving pre-write bytes indefinitely.
     */
    struct blkdev *parent;
    uint64_t       part_offset;

    /* For a raw disk registered via blkdev_register_disk(): the GEOM disk
     * whose partition child blkdevs derive from this device.  NULL for
     * partition blkdevs and for raw devices registered without partition
     * scanning.  Used to keep the partition bio caches coherent with raw
     * writes and to tear the partitions down when the raw device detaches. */
    struct geom_disk *geom;

    // VFS integration
    fs_node_t node;             // fs_node for DevFS
    struct blkdev *next;        // Linked list
} blkdev_t;

/*
 * Block-device ioctl requests.
 *
 * BLKIOC_FLUSH: push the DEVICE's own volatile write cache to media.  This is
 * a different thing from bufsync(), which only drains the kernel's bio cache
 * into the device -- where a modern disk is entitled to acknowledge the write
 * from DRAM and lose it on power failure.  arg is unused.  A driver whose
 * device has no write cache (or that cannot flush it) returns 0; -ENOTTY
 * means the driver has no ioctl at all.
 */
#define BLKIOC_FLUSH  0x4210

/* Ask every registered block device to flush its write cache.  Best effort:
 * devices with no ->ioctl are skipped.  Returns the number that reported a
 * failure. */
int blkdev_flush_all(void);

// Register a block device (creates DevFS entry)
void blkdev_register(blkdev_t *dev);
void blkdev_unregister(blkdev_t *dev);
void blkdev_scan_partitions(blkdev_t *dev);
void blkdev_register_disk(blkdev_t *dev);

// Get a block device by name
blkdev_t *blkdev_get(const char *name);

// First device in the registration list (walk with ->next).
blkdev_t *blkdev_first(void);

// Read from block device (byte-oriented wrapper)
size_t blkdev_read_bytes(blkdev_t *dev, uint64_t offset, size_t size, void *buffer);

// Write to block device (byte-oriented wrapper)
size_t blkdev_write_bytes(blkdev_t *dev, uint64_t offset, size_t size, const void *buffer);

#endif
