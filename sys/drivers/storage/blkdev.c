#include <kern/sched.h>
#include <string.h>

#include <drivers/storage/blkdev.h>
#include <kern/console.h>
#include <kern/geom/geom.h>
#include <sys/errno.h>
#include <sys/lock.h>
#include <vfs/buf.h>
#include <vfs/vfs.h>
#include <vm/vm_kmem.h>

#define STACK_BUF_SIZE 512

/*
 * Read-ahead window bounds (sectors).  The window starts at BLKDEV_RA_MIN on
 * the first detected sequential read and doubles each subsequent one up to
 * BLKDEV_RA_MAX -- a ramp that avoids over-reading for short sequential bursts
 * while reaching a deep window (128 KiB @ 512B sectors) for sustained streams.
 * BLKDEV_RA_MAX also sizes the per-device read-ahead scratch buffer.
 */
#define BLKDEV_RA_MIN   16
#define BLKDEV_RA_MAX   256
/* BLK-09: past this many sectors, invalidating a range one sector at a time
 * costs more than purging the device's whole cache. */
#define BLKDEV_INVAL_MAX 1024

static blkdev_t *blkdev_list = NULL;

/*
 * Serialises the registration list.  There is a live asynchronous producer:
 * USB unplug (usb_msc / uas -> scsi_unregister_link -> scsi_dev_detach ->
 * blkdev_unregister, which then memsets the embedded blkdev_t) runs against
 * VFS mount lookups walking the same list via blkdev_get().  Unsynchronised,
 * a walker could step into a node being unlinked and zeroed, or two
 * registrations could lose one another's head update.
 *
 * Held only across list mutation and traversal -- never across device I/O,
 * force-unmount or kfree, all of which can block.  No ISR touches the list.
 */
static spinlock_t blkdev_list_lock = SPINLOCK_INIT("blkdev_list");

typedef struct blkdev_geom_provider {
    blkdev_t *blkdev;
    geom_disk_t disk;
} blkdev_geom_provider_t;

// VFS read wrapper - translates byte reads to sector reads
static size_t blkdev_vfs_read(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
    blkdev_t *dev = (blkdev_t *)node->impl;
    if (!dev || !dev->read) return 0;
    // kprintf("blkdev_vfs: read %s offset=%lld size=%u\n", dev->name, offset, size);
    return blkdev_read_bytes(dev, offset, size, buffer);
}

// VFS write wrapper
static size_t blkdev_vfs_write(fs_node_t *node, off_t offset, size_t size, const uint8_t *buffer) {
    blkdev_t *dev = (blkdev_t *)node->impl;
    if (!dev || !dev->write) return 0;
    return blkdev_write_bytes(dev, offset, size, buffer);
}

static int blkdev_vfs_ioctl(fs_node_t *node, uint32_t request, void *arg) {
    blkdev_t *dev = (blkdev_t *)node->impl;

    if (!dev || !dev->ioctl) {
        return -ENOTTY;
    }
    return dev->ioctl(dev, request, arg);
}

void blkdev_register(blkdev_t *dev) {
    if (!dev) return;
    /* A driver registering with sector_size==0 silently produces a
     * zero-length device (length = total_sectors * 0).  Surface the
     * misconfiguration here rather than letting downstream callers
     * fail with mysterious EIOs. */
    if (dev->sector_size == 0) {
        kprintf("blkdev_register: refusing %s, sector_size is 0\n",
                dev->name[0] ? dev->name : "(unnamed)");
        return;
    }

    dev->dead = 0;

    // Setup VFS node
    memset(&dev->node, 0, sizeof(fs_node_t));
    strlcpy(dev->node.name, dev->name, sizeof(dev->node.name));
    dev->node.flags = FS_BLOCKDEVICE;
    /*
     * The memset above left mask/uid/gid at zero, so every disk node came out
     * mode 000 -- `ls -l /dev/storage` showed "b---------" for ide0, sata0,
     * scsi0 and friends alike.  Root bypasses the check so mounting still
     * worked, which is why this went unnoticed, but nothing running without
     * privilege could open a disk at all.  Use the same 0660 root:root the
     * character devices in drivers/devices/ set explicitly.
     */
    dev->node.mask = 0660;
    dev->node.uid = 0;
    dev->node.gid = 0;
    dev->node.length = dev->total_sectors * dev->sector_size;
    dev->node.impl = (uint32_t)(uintptr_t)dev;
    dev->node.read = blkdev_vfs_read;
    dev->node.write = blkdev_vfs_write;
    dev->node.ioctl = blkdev_vfs_ioctl;
    
    // Register with DevFS
    devfs_register_device(&dev->node);
    
    // Add to list
    spinlock_acquire(&blkdev_list_lock);
    dev->next = blkdev_list;
    blkdev_list = dev;
    spinlock_release(&blkdev_list_lock);
    
    kprintf("Block device /dev/storage/%s registered (%llu bytes)\n",
            dev->name, (unsigned long long)dev->node.length);
}

void blkdev_unregister(blkdev_t *dev) {
    blkdev_t **pp;

    if (!dev) return;

    /* Tear down the partition child devices first, while this raw device's
     * I/O still works, so their force-unmounts can flush.  geom_unregister_
     * disk() unregisters + frees each partition blkdev (force-unmount, devfs
     * removal, bio-cache purge) and drops the disk from the GEOM lists.  The
     * GEOM disk is embedded in the kmalloc'd provider, which we free after
     * (DRV-14).  Partition blkdevs have dev->geom == NULL, so this does not
     * recurse. */
    if (dev->geom) {
        geom_disk_t *disk = dev->geom;
        blkdev_geom_provider_t *provider =
            (blkdev_geom_provider_t *)((char *)disk -
                offsetof(blkdev_geom_provider_t, disk));
        /* Clear dev->geom BEFORE the walk.  geom_unregister_disk() unregisters
         * each partition, which force-unmounts it and flushes dirty buffers
         * back through blkdev_do_write() on this raw device -- and that calls
         * blkdev_invalidate_partitions(), which re-reads dev->geom and walks
         * the partition list currently being freed.  With the field already
         * NULL that walk is a no-op. */
        dev->geom = NULL;
        geom_unregister_disk(disk);
        kfree(provider, sizeof(*provider));
    }

    /* Mark dead FIRST so any in-flight/subsequent I/O short-circuits to -EIO
     * instead of touching a device that is gone (and, for the caller
     * scsi_dev_detach, a struct about to be memset to zero).  Then force-
     * unmount any filesystem still mounted on this device so the mount is torn
     * down before its backing blkdev disappears -- otherwise the mount's next
     * read/write dereferences a nulled callback and the kernel faults.  This
     * runs generically for every removable block device (USB mass storage,
     * ...), not just USB. */
    dev->dead = 1;
    vfs_force_unmount_dev(dev);

    spinlock_acquire(&blkdev_list_lock);
    pp = &blkdev_list;
    while (*pp) {
        if (*pp == dev) {
            *pp = dev->next;
            break;
        }
        pp = &(*pp)->next;
    }
    spinlock_release(&blkdev_list_lock);

    devfs_unregister_device(&dev->node);

    /* Drop every cached buffer keyed to this device so its memory is
     * freed and a future device reusing the pointer starts clean. */
    bio_dev_purge(dev);

    /*
     * BLK-04: wait for any in-flight prefetch to let go of ra_buf first.
     *
     * blkdev_prefetch checks dev->dead only on entry and then holds ra_busy
     * across a BLOCKING dev->read into dev->ra_buf.  Freeing here without
     * consulting ra_busy meant that on an unplug mid-stream the driver went
     * on to write up to 128 KiB into freed memory and then memcpy'd back out
     * of it.  The prefetch is bounded (one device read), so a spin with
     * preemption allowed is enough; the bound below is a backstop against a
     * driver that never returns, in which case leaking the buffer is far
     * better than freeing it underneath one.
     */
    if (dev->ra_buf) {
        int spins = 0;
        while (__atomic_load_n(&dev->ra_busy, __ATOMIC_ACQUIRE)) {
            if (++spins > 100000) {
                kprintf("blkdev: %s unregistered with a prefetch still in "
                        "flight; leaking its read-ahead buffer\n", dev->name);
                dev->ra_buf = NULL;     /* deliberately not freed */
                break;
            }
            sched_yield();
        }
    }
    if (dev->ra_buf) {
        kfree(dev->ra_buf, (size_t)BLKDEV_RA_MAX * dev->ra_ss);
        dev->ra_buf = NULL;
        dev->ra_ss  = 0;
    }

    dev->next = NULL;
}

/*
 * Block-level buffer cache.
 *
 * All sector I/O funnels through blkdev_do_read / blkdev_do_write, which use
 * the shared bio.c buffer cache keyed by (blkdev *, sector).  This is the
 * single, transparent, driver-agnostic disk cache: any device is cached the
 * moment it registers a blkdev_t, and no filesystem carries caching logic.
 * Device I/O always runs with the bio buffer merely B_BUSY and the bio
 * spinlock dropped, so a blocking driver (AHCI/IDE/NVMe) never issues I/O
 * while holding a spinlock.
 */

/*
 * Prefetch up to `window` sectors starting at `start` into the buffer cache.
 * Reads only the leading run that is not already cached (one device read) into
 * the device's persistent read-ahead scratch and populates the cache with it;
 * the streaming reader then serves those sectors as cache hits instead of one
 * device round-trip per read.
 *
 * The scratch buffer is per-device and allocated once (BLKDEV_RA_MAX sectors),
 * so a long stream never re-allocates -- avoiding the buddy-allocator
 * fragmentation a per-prefetch 128 KiB contiguous alloc would cause.  ra_busy
 * serialises access to it: a reader that finds a prefetch already in flight on
 * this device just skips its own (best-effort -- that fill covers the frontier
 * anyway).  Any allocation or device-read failure simply skips the prefetch.
 */
static void blkdev_prefetch(blkdev_t *dev, uint64_t start, uint32_t window) {
    if (dev->dead || !dev->read || start >= dev->total_sectors)
        return;

    uint32_t ss = dev->sector_size;
    if (window > BLKDEV_RA_MAX)
        window = BLKDEV_RA_MAX;
    if ((uint64_t)window > dev->total_sectors - start)
        window = (uint32_t)(dev->total_sectors - start);
    if (window == 0)
        return;

    /* One prefetch per device at a time -- it owns the shared scratch buffer. */
    if (__atomic_exchange_n(&dev->ra_busy, 1, __ATOMIC_ACQUIRE))
        return;

    /*
     * BLK-07: the scratch was sized from the sector size at FIRST prefetch and
     * never revalidated, so an ATAPI device switching between 512 and 2048
     * either overflowed it (2048 after being sized for 512) or had its kfree
     * mis-sized on teardown.  Remember what it was sized for and re-allocate
     * when the device's sector size changes underneath us.
     */
    if (dev->ra_buf && dev->ra_ss != ss) {
        kfree(dev->ra_buf, (size_t)BLKDEV_RA_MAX * dev->ra_ss);
        dev->ra_buf = NULL;
        dev->ra_ss  = 0;
    }
    if (!dev->ra_buf) {
        dev->ra_buf = kmalloc((size_t)BLKDEV_RA_MAX * ss);
        if (!dev->ra_buf) {
            __atomic_store_n(&dev->ra_busy, 0, __ATOMIC_RELEASE);
            return;
        }
        dev->ra_ss = ss;
    }

    /* Only fetch the leading contiguous run of not-yet-cached sectors: past
     * the frontier there is nothing to gain, and stopping at the first cached
     * sector keeps the prefetch a single coalesced device read. */
    uint32_t run = 0;
    while (run < window && !bio_dev_cached(dev, (int64_t)(start + run)))
        run++;

    if (run > 0 && dev->read(dev, start, run, dev->ra_buf) == 0) {
        for (uint32_t k = 0; k < run; k++) {
            struct buf *bp = bio_dev_get(dev, (int64_t)(start + k), ss);
            if (!bp)
                continue;                   /* cache full: drop the rest */
            if (!(bp->b_flags & B_CACHE)) {
                memcpy(bp->b_data, (uint8_t *)dev->ra_buf + (size_t)k * ss, ss);
                bp->b_flags |= B_CACHE;
            }
            bio_dev_release(bp);
        }
    }

    __atomic_store_n(&dev->ra_busy, 0, __ATOMIC_RELEASE);
}

/*
 * Read `count` sectors at `sector` into `buffer`: serve cached sectors from
 * the buffer cache and coalesce each maximal run of contiguous uncached
 * sectors into one device read.  Returns 0 on success, else the driver's
 * error.
 */
static int blkdev_do_read(blkdev_t *dev, uint64_t sector, uint32_t count, void *buffer) {
    if (dev->dead) return -EIO;   /* removed/unplugged: no I/O to a gone device */
    uint32_t ss = dev->sector_size;
    uint8_t *out = (uint8_t *)buffer;
    uint32_t i = 0;

    while (i < count) {
        struct buf *bp = bio_dev_get(dev, (int64_t)(sector + i), ss);
        if (!bp) {
            /* Cache exhausted (low memory): read the remainder directly. */
            return dev->read(dev, sector + i, count - i, out + (size_t)i * ss);
        }

        if (bp->b_flags & B_CACHE) {            /* hit */
            memcpy(out + (size_t)i * ss, bp->b_data, ss);
            bio_dev_release(bp);
            i++;
            continue;
        }

        /* Miss at sector+i: extend the run over contiguous misses so a cold
         * sequential read issues one device I/O instead of one per sector
         * (BLK-12).  The probe is a heuristic; under write-through any cached
         * sector re-read here would yield identical bytes, so a misjudged
         * run is harmless. */
        uint32_t run = 1;
        while (i + run < count && !bio_dev_cached(dev, (int64_t)(sector + i + run)))
            run++;

        int ret = dev->read(dev, sector + i, run, out + (size_t)i * ss);
        if (ret != 0) {
            bp->b_flags |= B_INVAL;             /* never cache a failed read */
            bio_dev_release(bp);
            return ret;
        }

        /* Populate the cache: lead sector via the buffer we already hold,
         * the rest one at a time -- holding only one busy buffer at a time
         * avoids any multi-buffer lock-ordering hazard. */
        memcpy(bp->b_data, out + (size_t)i * ss, ss);
        bp->b_flags |= B_CACHE;
        bio_dev_release(bp);

        for (uint32_t k = 1; k < run; k++) {
            struct buf *bk = bio_dev_get(dev, (int64_t)(sector + i + k), ss);
            if (!bk)
                continue;                       /* cache full: data still in `out` */
            if (!(bk->b_flags & B_CACHE)) {
                memcpy(bk->b_data, out + (size_t)(i + k) * ss, ss);
                bk->b_flags |= B_CACHE;
            }
            bio_dev_release(bk);
        }
        i += run;
    }

    /*
     * Sequential read-ahead.  If this request continued exactly where the
     * previous one ended, ramp the read-ahead window and prefetch that many
     * sectors past the end of this request into the cache -- but only when the
     * sector just past the end is not already cached (i.e. we are at the
     * frontier), so a reader working through an already-prefetched region
     * costs a single cache probe and no device I/O.  A non-sequential request
     * resets the window so random access does not drag unwanted sectors in.
     */
    uint64_t end = sector + count;
    if (sector == dev->ra_next) {
        uint32_t win = dev->ra_window ? dev->ra_window * 2 : BLKDEV_RA_MIN;
        if (win > BLKDEV_RA_MAX)
            win = BLKDEV_RA_MAX;
        dev->ra_window = win;
        if (end < dev->total_sectors && !bio_dev_cached(dev, (int64_t)end))
            blkdev_prefetch(dev, end, win);
    } else {
        dev->ra_window = 0;
    }
    dev->ra_next = end;
    return 0;
}

/*
 * A raw-disk write bypasses the per-partition bio caches: a partition's
 * sectors are cached under its OWN blkdev pointer at partition-relative
 * offsets, so a write to the raw disk node at absolute sector S leaves the
 * partition's cached copy of that same physical sector stale (DRV-13).
 * Drop every partition cache block overlapping the written range.  A no-op
 * for partition blkdevs and for raw devices with no partitions (dev->geom
 * NULL).
 */
static void blkdev_invalidate_partitions(blkdev_t *dev, uint64_t sector, uint32_t count) {
    geom_disk_t *disk = dev->geom;
    uint64_t w_start, w_end;

    /*
     * BLK-03: the partition -> raw direction.  A write through a partition
     * node leaves the RAW device's cache of those same physical sectors
     * stale, and this used to return right here because a partition blkdev
     * has no ->geom.
     *
     * BLK-09: invalidate per sector only for small ranges.  A large raw
     * write (up to UINT32_MAX sectors) turned this into ~500K hash lookups
     * per call; past a threshold, purge the device wholesale instead.
     */
    if (!disk) {
        if (dev->parent) {
            if (count > BLKDEV_INVAL_MAX) {
                bio_dev_purge(dev->parent);
            } else {
                for (uint32_t i = 0; i < count; i++)
                    bio_dev_invalidate(dev->parent,
                                       (int64_t)(dev->part_offset + sector + i));
            }
        }
        return;
    }

    w_start = sector;
    w_end   = sector + count;               /* exclusive */
    for (geom_partition_t *p = disk->partitions; p; p = p->next) {
        uint64_t p_start, p_end, lo, hi;

        if (!p->bdev) continue;
        p_start = p->start_lba;
        p_end   = p->start_lba + p->size_sectors;
        lo = (w_start > p_start) ? w_start : p_start;
        hi = (w_end   < p_end)   ? w_end   : p_end;
        if (hi <= lo) continue;
        if (hi - lo > BLKDEV_INVAL_MAX) {      /* BLK-09 */
            bio_dev_purge(p->bdev);
            continue;
        }
        for (uint64_t s = lo; s < hi; s++)
            bio_dev_invalidate(p->bdev, (int64_t)(s - p_start));
    }
}

/*
 * Write `count` sectors write-through: push to the device first, then
 * refresh the cached copies (BLK-5).  On a failed device write, invalidate
 * the affected cached sectors so no stale data is ever served (BLK-6).
 */
static int blkdev_do_write(blkdev_t *dev, uint64_t sector, uint32_t count, const void *buffer) {
    if (dev->dead) return -EIO;   /* removed/unplugged: no I/O to a gone device */
    uint32_t ss = dev->sector_size;
    const uint8_t *in = (const uint8_t *)buffer;

    int ret = dev->write(dev, sector, count, buffer);

    /* Keep any overlapping partition-cache blocks coherent with this raw
     * write regardless of outcome (on failure the on-disk contents are
     * indeterminate, so a stale cached copy must not survive either). */
    blkdev_invalidate_partitions(dev, sector, count);

    if (ret != 0) {
        for (uint32_t k = 0; k < count; k++)
            bio_dev_invalidate(dev, (int64_t)(sector + k));
        return ret;
    }

    for (uint32_t k = 0; k < count; k++) {
        struct buf *bp = bio_dev_get(dev, (int64_t)(sector + k), ss);
        if (!bp)
            continue;
        memcpy(bp->b_data, in + (size_t)k * ss, ss);
        bp->b_flags |= B_CACHE;
        bio_dev_release(bp);
    }
    return 0;
}

static int blkdev_geom_read(struct geom_disk *disk, uint64_t sector, size_t count, void *buf) {
    blkdev_geom_provider_t *provider = (blkdev_geom_provider_t *)disk->priv;
    /* BLK-08: a bare -1 reaches userland as EPERM ("operation not
     * permitted") for what is really a missing device or a bad argument. */
    if (!provider || !provider->blkdev || !provider->blkdev->read) return -ENXIO;
    if (count > 0xFFFFFFFFU) return -EINVAL;
    /* Honour the `dead` short-circuit (DRV-13) but read directly from the
     * driver: geom_read runs during partition scan at registration time
     * (before the raw device has any mount/bio context), and its reads are
     * keyed on the raw-disk pointer at absolute sectors -- a different key
     * from a partition blkdev's own cache -- so routing through the buffer
     * cache neither dedupes with partition reads nor is safe this early. */
    if (provider->blkdev->dead) return -EIO;
    return provider->blkdev->read(provider->blkdev, sector, (uint32_t)count, buf);
}

static int blkdev_geom_write(struct geom_disk *disk, uint64_t sector, size_t count, const void *buf) {
    blkdev_geom_provider_t *provider = (blkdev_geom_provider_t *)disk->priv;
    if (!provider || !provider->blkdev || !provider->blkdev->write) return -ENXIO;
    if (count > 0xFFFFFFFFU) return -EINVAL;
    return blkdev_do_write(provider->blkdev, sector, (uint32_t)count, buf);
}

void blkdev_scan_partitions(blkdev_t *dev) {
    if (!dev) return;

    blkdev_geom_provider_t *provider = kmalloc(sizeof(*provider));
    if (!provider) {
        kprintf("blkdev: failed to allocate GEOM provider for %s\n", dev->name);
        return;
    }

    memset(provider, 0, sizeof(*provider));
    provider->blkdev = dev;
    strlcpy(provider->disk.name, dev->name, sizeof(provider->disk.name));
    provider->disk.name[sizeof(provider->disk.name) - 1] = '\0';
    provider->disk.priv = provider;
    provider->disk.read = blkdev_geom_read;
    provider->disk.write = dev->write ? blkdev_geom_write : NULL;
    provider->disk.total_sectors = dev->total_sectors;
    provider->disk.sector_size = dev->sector_size;

    /* Back-link the raw device to its GEOM disk so raw writes can keep the
     * partition bio caches coherent and detach can tear the partitions down. */
    dev->geom = &provider->disk;

    geom_register_disk(&provider->disk);
}

void blkdev_register_disk(blkdev_t *dev) {
    if (!dev) return;

    /*
     * BLK-05: blkdev_register() refuses a device with sector_size == 0, but
     * the scan ran regardless, publishing a geom_disk_t (and partition
     * blkdevs) for a device the block layer does not know about.  And a
     * second scan overwrote dev->geom, orphaning the previous provider.
     */
    if (dev->sector_size == 0)
        return;                 /* blkdev_register() would refuse it too */
    blkdev_register(dev);
    if (dev->geom)
        return;                 /* already scanned; re-scan would leak */

    blkdev_scan_partitions(dev);

    /*
     * BLK-03: give each partition blkdev a back-pointer to this raw device.
     *
     * blkdev_invalidate_partitions() only ever walked raw -> partition, and
     * returned immediately when dev->geom was NULL -- which is exactly the
     * case for every partition blkdev.  So a write through ide0p1 never
     * invalidated the raw ide0 cache entry for the same physical sector, and
     * raw reads kept serving pre-write bytes indefinitely.  This is the
     * mirror direction; part_offset was recorded at partition creation.
     */
    if (dev->geom) {
        for (geom_partition_t *p = dev->geom->partitions; p; p = p->next) {
            if (p->bdev) p->bdev->parent = dev;
        }
    }
}

blkdev_t *blkdev_get(const char *name) {
    blkdev_t *dev;

    spinlock_acquire(&blkdev_list_lock);
    for (dev = blkdev_list; dev; dev = dev->next) {
        if (strcmp(dev->name, name) == 0) break;
    }
    spinlock_release(&blkdev_list_lock);
    /* NOTE: the caller gets an unreferenced pointer.  The list walk is now
     * safe, but nothing yet stops the device being unregistered between this
     * return and the caller's use -- that needs refcounting on blkdev_t and
     * is deliberately left for a follow-up (#403). */
    return dev;
}

/* First device in the registration list; walk with ->next.  Used by the
 * VFS to scan every block device when resolving a LABEL=<name> mount. */
blkdev_t *blkdev_first(void) {
    return blkdev_list;
}

/*
 * [AHCI-18] Push every device's own write cache to media.
 *
 * bufsync() drains the kernel's bio cache INTO the devices; it does not make
 * the devices durable.  A disk with write caching enabled acknowledges a
 * write as soon as it is in the drive's DRAM, so after sync(2) returned,
 * everything above the driver believed the data was safe while the disk had
 * not necessarily written a byte of it.  Drivers that can flush implement
 * BLKIOC_FLUSH; the rest report -ENOTTY and are skipped.
 *
 * Only raw disks are asked: a partition blkdev shares its parent's cache, so
 * flushing each partition would issue the same FLUSH CACHE N times.
 */
int blkdev_flush_all(void) {
    blkdev_t *dev;
    int failures = 0;

    /*
     * The list is walked without the lock held across the ioctl: flushing is
     * a sleeping operation (it issues a command and waits) and
     * blkdev_list_lock is a spinlock that disables preemption.  Registration
     * only ever appends, and unregistration is not concurrent with sync in
     * practice; the same caveat as blkdev_get() applies until blkdev_t is
     * refcounted (#403).
     */
    for (dev = blkdev_list; dev; dev = dev->next) {
        int r;

        if (!dev->ioctl || dev->dead)
            continue;
        if (dev->parent)                /* partition: parent covers it */
            continue;

        r = dev->ioctl(dev, BLKIOC_FLUSH, NULL);
        if (r < 0 && r != -ENOTTY) {
            kprintf("blkdev: %s: cache flush failed (%d)\n",
                    dev->name[0] ? dev->name : "(unnamed)", r);
            failures++;
        }
    }

    return failures;
}

// Byte-oriented read - handles sector alignment
size_t blkdev_read_bytes(blkdev_t *dev, uint64_t offset, size_t size, void *buffer) {
    if (!dev || !dev->read || dev->sector_size == 0) return 0;
    if (size == 0) return 0;
    
    uint32_t sector_size = dev->sector_size;
    uint64_t start_sector = offset / sector_size;
    uint32_t sector_offset = offset % sector_size;
    size_t total_read = 0;
    uint8_t *buf = (uint8_t *)buffer;

    /*
     * Past the end is a normal answer, not an error: returning 0 IS the
     * report, and callers are built around it.  libext2fs's
     * ext2fs_get_device_size2() -- so every e2fsck the boot runs -- finds a
     * device's size by binary-searching for the last readable sector, which
     * means deliberately reading off the end several times per device and
     * narrowing down.  Announcing each of those printed a descending ladder
     * of "EOF" lines on every fsck:
     *
     *   blkdev: scsi0p2 EOF (sector 8388608 >= 8284160)
     *   blkdev: scsi0p2 EOF (sector 8323072 >= 8284160)
     *   ... down to the real end at 8284160
     *
     * The same message had already had to be worked around once from the
     * other side -- see the no-media probe skip in vfs.c's label scan -- and
     * a diagnostic that callers keep having to avoid tripping is not earning
     * its place on the console. [HW-06]
     */
    if (start_sector >= dev->total_sectors)
        return 0;
    uint64_t sectors_left = dev->total_sectors - start_sector;
    uint64_t bytes_left = (sectors_left > UINT64_MAX / sector_size) ?
        UINT64_MAX : sectors_left * sector_size;
    if (bytes_left <= sector_offset) return 0;
    bytes_left -= sector_offset;
    if ((uint64_t)size > bytes_left) size = (size_t)bytes_left;
    
    // Use stack buffer for small sectors to avoid kmalloc overhead
    uint8_t stack_buf[STACK_BUF_SIZE];
    uint8_t *sector_buf = NULL;
    
    // 1. Handle unaligned start
    if (sector_offset > 0) {
        if (sector_size <= STACK_BUF_SIZE) {
            sector_buf = stack_buf;
        } else {
            sector_buf = kmalloc(sector_size);
            if (!sector_buf) return 0;
        }

        if (blkdev_do_read(dev, start_sector, 1, sector_buf) != 0) {
            if (sector_buf && sector_buf != stack_buf) kfree(sector_buf, sector_size);
            return 0; // Read error
        }
        
        uint32_t copy_size = sector_size - sector_offset;
        if (copy_size > size) copy_size = size;
        
        memcpy(buf, sector_buf + sector_offset, copy_size);
        
        buf += copy_size;
        total_read += copy_size;
        size -= copy_size;
        start_sector++;
    }

    // 2. Handle aligned full sectors (Bulk Read)
    while (size >= sector_size) {
        uint64_t sectors = size / sector_size;
        if (sectors > UINT32_MAX) sectors = UINT32_MAX;
        if (sectors > dev->total_sectors - start_sector)
            sectors = dev->total_sectors - start_sector;
        /*
         * BLK-06: leaving the bulk loop with size still >= sector_size means
         * the tail copy below would memcpy size >= 512 bytes out of the
         * 512-byte stack_buf.  The clamps above currently maintain the
         * invariant that this cannot happen, but that depends on
         * dev->total_sectors being stable, and fdc_refresh_geometry rewrites
         * it mid-I/O on a media change.  Make the violation an explicit stop
         * rather than a silent overrun, and let the tail clamp below bound
         * the copy regardless.
         */
        if (sectors == 0) {
            if (size >= sector_size) {
                kprintf("blkdev: %s geometry changed mid-transfer; "
                        "truncating request\n", dev->name);
                size = 0;
            }
            break;
        }
        uint32_t sector_count = (uint32_t)sectors;

        // Cached + coalesced read straight into the user buffer.
        if (blkdev_do_read(dev, start_sector, sector_count, buf) != 0) {
            if (sector_buf && sector_buf != stack_buf) kfree(sector_buf, sector_size);
            return total_read;
        }

        size_t bytes_read = (size_t)sector_count * sector_size;
        buf += bytes_read;
        total_read += bytes_read;
        size -= bytes_read;
        start_sector += sector_count;
    }
    
    // 3. Handle unaligned end (tail)
    if (size > 0) {
        if (!sector_buf) {
            if (sector_size <= STACK_BUF_SIZE) {
                sector_buf = stack_buf;
            } else {
                sector_buf = kmalloc(sector_size);
                if (!sector_buf) return total_read;
            }
        }

        if (blkdev_do_read(dev, start_sector, 1, sector_buf) != 0) {
            if (sector_buf && sector_buf != stack_buf) kfree(sector_buf, sector_size);
            return total_read;
        }
        /* BLK-06: the tail is by definition a partial sector; clamp so the
         * copy can never exceed the buffer even if `size` arrived larger. */
        size_t tail = size < sector_size ? size : sector_size;
        memcpy(buf, sector_buf, tail);
        total_read += tail;
    }

    if (sector_buf && sector_buf != stack_buf) kfree(sector_buf, sector_size);
    return total_read;
}

// Byte-oriented write - handles sector alignment (read-modify-write for unaligned)
size_t blkdev_write_bytes(blkdev_t *dev, uint64_t offset, size_t size, const void *buffer) {
    if (!dev || !dev->write || dev->sector_size == 0) return 0;
    if (size == 0) return 0;
    
    uint32_t sector_size = dev->sector_size;
    uint64_t start_sector = offset / sector_size;
    uint32_t sector_offset = offset % sector_size;
    size_t total_written = 0;
    const uint8_t *buf = (const uint8_t *)buffer;

    if (start_sector >= dev->total_sectors) return 0;
    uint64_t sectors_left = dev->total_sectors - start_sector;
    uint64_t bytes_left = (sectors_left > UINT64_MAX / sector_size) ?
        UINT64_MAX : sectors_left * sector_size;
    if (bytes_left <= sector_offset) return 0;
    bytes_left -= sector_offset;
    if ((uint64_t)size > bytes_left) size = (size_t)bytes_left;
    
    // Use stack buffer for small sectors to avoid kmalloc overhead
    uint8_t stack_buf[STACK_BUF_SIZE];
    uint8_t *sector_buf = NULL;
    
    // 1. Handle unaligned start
    if (sector_offset > 0) {
        if (!dev->read) return 0;
        if (sector_size <= STACK_BUF_SIZE) {
            sector_buf = stack_buf;
        } else {
            sector_buf = kmalloc(sector_size);
            if (!sector_buf) return 0;
        }

        uint32_t copy_size = sector_size - sector_offset;
        if (copy_size > size) copy_size = size;
        
        // Read-Modify-Write
        if (blkdev_do_read(dev, start_sector, 1, sector_buf) != 0) {
            if (sector_buf && sector_buf != stack_buf) kfree(sector_buf, sector_size);
            return 0;
        }
        
        memcpy(sector_buf + sector_offset, buf, copy_size);
        
        if (blkdev_do_write(dev, start_sector, 1, sector_buf) != 0) {
            if (sector_buf && sector_buf != stack_buf) kfree(sector_buf, sector_size);
            return 0;
        }
        
        buf += copy_size;
        total_written += copy_size;
        size -= copy_size;
        start_sector++;
    }
    
    // 2. Handle aligned full sectors (Bulk Write)
    while (size >= sector_size) {
        uint64_t sectors = size / sector_size;
        if (sectors > UINT32_MAX) sectors = UINT32_MAX;
        if (sectors > dev->total_sectors - start_sector)
            sectors = dev->total_sectors - start_sector;
        /*
         * BLK-06: leaving the bulk loop with size still >= sector_size means
         * the tail copy below would memcpy size >= 512 bytes out of the
         * 512-byte stack_buf.  The clamps above currently maintain the
         * invariant that this cannot happen, but that depends on
         * dev->total_sectors being stable, and fdc_refresh_geometry rewrites
         * it mid-I/O on a media change.  Make the violation an explicit stop
         * rather than a silent overrun, and let the tail clamp below bound
         * the copy regardless.
         */
        if (sectors == 0) {
            if (size >= sector_size) {
                kprintf("blkdev: %s geometry changed mid-transfer; "
                        "truncating request\n", dev->name);
                size = 0;
            }
            break;
        }
        uint32_t sector_count = (uint32_t)sectors;

        // Write-through the cache from the user buffer.
        if (blkdev_do_write(dev, start_sector, sector_count, buf) != 0) {
            if (sector_buf && sector_buf != stack_buf) kfree(sector_buf, sector_size);
            return total_written;
        }

        size_t bytes_written = (size_t)sector_count * sector_size;
        buf += bytes_written;
        total_written += bytes_written;
        size -= bytes_written;
        start_sector += sector_count;
    }

    // 3. Handle unaligned end (tail)
    if (size > 0) {
        if (!dev->read) return total_written;
        if (!sector_buf) {
            if (sector_size <= STACK_BUF_SIZE) {
                sector_buf = stack_buf;
            } else {
                sector_buf = kmalloc(sector_size);
                if (!sector_buf) return total_written;
            }
        }

        // Read-Modify-Write
        if (blkdev_do_read(dev, start_sector, 1, sector_buf) != 0) {
            if (sector_buf && sector_buf != stack_buf) kfree(sector_buf, sector_size);
            return total_written;
        }

        memcpy(sector_buf, buf, size);

        if (blkdev_do_write(dev, start_sector, 1, sector_buf) != 0) {
            if (sector_buf && sector_buf != stack_buf) kfree(sector_buf, sector_size);
            return total_written;
        }

        total_written += size;
    }

    if (sector_buf && sector_buf != stack_buf) kfree(sector_buf, sector_size);
    return total_written;
}
