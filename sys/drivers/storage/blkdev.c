#include <drivers/storage/blkdev.h>
#include <vfs/buf.h>
#include <vfs/vfs.h>
#include <kern/geom/geom.h>
#include <kern/console.h>
#include <sys/errno.h>
#include <sys/lock.h>
#include <string.h>
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

static blkdev_t *blkdev_list = NULL;

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
    dev->node.length = dev->total_sectors * dev->sector_size;
    dev->node.impl = (uint32_t)(uintptr_t)dev;
    dev->node.read = blkdev_vfs_read;
    dev->node.write = blkdev_vfs_write;
    dev->node.ioctl = blkdev_vfs_ioctl;
    
    // Register with DevFS
    devfs_register_device(&dev->node);
    
    // Add to list
    dev->next = blkdev_list;
    blkdev_list = dev;
    
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
        blkdev_geom_provider_t *provider =
            (blkdev_geom_provider_t *)((char *)dev->geom -
                offsetof(blkdev_geom_provider_t, disk));
        geom_unregister_disk(dev->geom);
        dev->geom = NULL;
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

    pp = &blkdev_list;
    while (*pp) {
        if (*pp == dev) {
            *pp = dev->next;
            break;
        }
        pp = &(*pp)->next;
    }

    devfs_unregister_device(&dev->node);

    /* Drop every cached buffer keyed to this device so its memory is
     * freed and a future device reusing the pointer starts clean. */
    bio_dev_purge(dev);

    /* Release the read-ahead scratch (allocated lazily on first prefetch). */
    if (dev->ra_buf) {
        kfree(dev->ra_buf, (size_t)BLKDEV_RA_MAX * dev->sector_size);
        dev->ra_buf = NULL;
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

    if (!dev->ra_buf) {
        dev->ra_buf = kmalloc((size_t)BLKDEV_RA_MAX * ss);
        if (!dev->ra_buf) {
            __atomic_store_n(&dev->ra_busy, 0, __ATOMIC_RELEASE);
            return;
        }
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

    if (!disk) return;

    w_start = sector;
    w_end   = sector + count;               /* exclusive */
    for (geom_partition_t *p = disk->partitions; p; p = p->next) {
        uint64_t p_start, p_end, lo, hi;

        if (!p->bdev) continue;
        p_start = p->start_lba;
        p_end   = p->start_lba + p->size_sectors;
        lo = (w_start > p_start) ? w_start : p_start;
        hi = (w_end   < p_end)   ? w_end   : p_end;
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
    if (!provider || !provider->blkdev || !provider->blkdev->read) return -1;
    if (count > 0xFFFFFFFFU) return -1;
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
    if (!provider || !provider->blkdev || !provider->blkdev->write) return -1;
    if (count > 0xFFFFFFFFU) return -1;
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

    blkdev_register(dev);
    blkdev_scan_partitions(dev);
}

blkdev_t *blkdev_get(const char *name) {
    blkdev_t *dev = blkdev_list;
    while (dev) {
        if (strcmp(dev->name, name) == 0) return dev;
        dev = dev->next;
    }
    return NULL;
}

/* First device in the registration list; walk with ->next.  Used by the
 * VFS to scan every block device when resolving a LABEL=<name> mount. */
blkdev_t *blkdev_first(void) {
    return blkdev_list;
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

    if (start_sector >= dev->total_sectors) {
        kprintf("blkdev: %s EOF (sector %llu >= %llu)\n", dev->name, start_sector, dev->total_sectors);
        return 0;
    }
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
        if (sectors == 0) break;
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
        memcpy(buf, sector_buf, size);
        total_read += size;
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
        if (sectors == 0) break;
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
