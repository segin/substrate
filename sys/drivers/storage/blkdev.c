#include <drivers/storage/blkdev.h>
#include <kern/geom/geom.h>
#include <kern/console.h>
#include <sys/errno.h>
#include <sys/lock.h>
#include <string.h>
#include <vm/vm_kmem.h>

#define STACK_BUF_SIZE 512

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

    // Setup VFS node
    memset(&dev->node, 0, sizeof(fs_node_t));
    strncpy(dev->node.name, dev->name, sizeof(dev->node.name) - 1);
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
    
    kprintf("Block device /dev/storage/%s registered (%u bytes)\n", 
            dev->name, (uint32_t)dev->node.length);
}

void blkdev_unregister(blkdev_t *dev) {
    blkdev_t **pp;

    if (!dev) return;

    pp = &blkdev_list;
    while (*pp) {
        if (*pp == dev) {
            *pp = dev->next;
            break;
        }
        pp = &(*pp)->next;
    }

    devfs_unregister_device(&dev->node);
    dev->next = NULL;
}

#define BCACHE_ENTRIES 1024
typedef struct {
    blkdev_t *dev;
    uint64_t sector;
    uint32_t flags; // 1 = valid, 2 = dirty
    uint64_t lru_time;
    uint8_t *data; // allocated via kmalloc
} bcache_entry_t;

static bcache_entry_t bcache[BCACHE_ENTRIES];
static uint64_t bcache_clock = 0;
static spinlock_t bcache_lock;
static int bcache_initialized = 0;

static void bcache_init(void) {
    spinlock_init(&bcache_lock, "bcache");
    memset(bcache, 0, sizeof(bcache));
    bcache_initialized = 1;
}

static bcache_entry_t *bcache_lookup(blkdev_t *dev, uint64_t sector) {
    for (int i = 0; i < BCACHE_ENTRIES; i++) {
        if ((bcache[i].flags & 1) && bcache[i].dev == dev && bcache[i].sector == sector) {
            bcache[i].lru_time = ++bcache_clock;
            return &bcache[i];
        }
    }
    return NULL;
}

static bcache_entry_t *bcache_evict(void) {
    uint64_t oldest = UINT64_MAX;
    int oldest_idx = 0;
    for (int i = 0; i < BCACHE_ENTRIES; i++) {
        if (!(bcache[i].flags & 1)) return &bcache[i];
        if (bcache[i].lru_time < oldest) {
            oldest = bcache[i].lru_time;
            oldest_idx = i;
        }
    }
    bcache_entry_t *entry = &bcache[oldest_idx];
    if (entry->flags & 2) {
        entry->dev->write(entry->dev, entry->sector, 1, entry->data);
        entry->flags &= ~2;
    }
    return entry;
}

static void bcache_invalidate(blkdev_t *dev, uint64_t start_sector, uint32_t count) {
    if (!bcache_initialized) return;
    spinlock_acquire(&bcache_lock);
    for (int i = 0; i < BCACHE_ENTRIES; i++) {
        if ((bcache[i].flags & 1) && bcache[i].dev == dev && 
            bcache[i].sector >= start_sector && bcache[i].sector < start_sector + count) {
            if (bcache[i].flags & 2) {
                bcache[i].dev->write(bcache[i].dev, bcache[i].sector, 1, bcache[i].data);
            }
            bcache[i].flags = 0;
        }
    }
    spinlock_release(&bcache_lock);
}

static int blkdev_do_read(blkdev_t *dev, uint64_t sector, uint32_t count, void *buffer) {
    if (count > 1) {
        bcache_invalidate(dev, sector, count);
        return dev->read(dev, sector, count, buffer);
    }
    if (!bcache_initialized) bcache_init();
    spinlock_acquire(&bcache_lock);
    bcache_entry_t *entry = bcache_lookup(dev, sector);
    if (entry) {
        memcpy(buffer, entry->data, dev->sector_size);
        spinlock_release(&bcache_lock);
        return 0;
    }
    entry = bcache_evict();
    if (!entry->data) {
        entry->data = kmalloc(dev->sector_size);
        if (!entry->data) {
            spinlock_release(&bcache_lock);
            return dev->read(dev, sector, 1, buffer);
        }
    }
    int ret = dev->read(dev, sector, 1, entry->data);
    if (ret == 0) {
        entry->dev = dev;
        entry->sector = sector;
        entry->flags = 1;
        entry->lru_time = ++bcache_clock;
        memcpy(buffer, entry->data, dev->sector_size);
    } else {
        entry->flags = 0;
    }
    spinlock_release(&bcache_lock);
    return ret;
}

static int blkdev_do_write(blkdev_t *dev, uint64_t sector, uint32_t count, const void *buffer) {
    if (count > 1) {
        bcache_invalidate(dev, sector, count);
        return dev->write(dev, sector, count, buffer);
    }
    if (!bcache_initialized) bcache_init();
    spinlock_acquire(&bcache_lock);
    bcache_entry_t *entry = bcache_lookup(dev, sector);
    if (!entry) {
        entry = bcache_evict();
        if (!entry->data) {
            entry->data = kmalloc(dev->sector_size);
            if (!entry->data) {
                spinlock_release(&bcache_lock);
                return dev->write(dev, sector, 1, buffer);
            }
        }
        entry->dev = dev;
        entry->sector = sector;
    }
    memcpy(entry->data, buffer, dev->sector_size);
    entry->flags = 3;
    entry->lru_time = ++bcache_clock;
    int ret = dev->write(dev, sector, 1, buffer);
    entry->flags &= ~2;
    spinlock_release(&bcache_lock);
    return ret;
}

static int blkdev_geom_read(struct geom_disk *disk, uint64_t sector, size_t count, void *buf) {
    blkdev_geom_provider_t *provider = (blkdev_geom_provider_t *)disk->priv;
    if (!provider || !provider->blkdev || !provider->blkdev->read) return -1;
    if (count > 0xFFFFFFFFU) return -1;
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
    strncpy(provider->disk.name, dev->name, sizeof(provider->disk.name) - 1);
    provider->disk.name[sizeof(provider->disk.name) - 1] = '\0';
    provider->disk.priv = provider;
    provider->disk.read = blkdev_geom_read;
    provider->disk.write = dev->write ? blkdev_geom_write : NULL;
    provider->disk.total_sectors = dev->total_sectors;
    provider->disk.sector_size = dev->sector_size;

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

        // Read directly into user buffer
        if (dev->read(dev, start_sector, sector_count, buf) != 0) {
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

        // Write directly from user buffer
        if (dev->write(dev, start_sector, sector_count, buf) != 0) {
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
