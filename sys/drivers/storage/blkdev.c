#include <drivers/storage/blkdev.h>
#include <kern/geom/geom.h>
#include <kern/console.h>
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
    return blkdev_read_bytes(dev, offset, size, buffer);
}

// VFS write wrapper
static size_t blkdev_vfs_write(fs_node_t *node, off_t offset, size_t size, const uint8_t *buffer) {
    blkdev_t *dev = (blkdev_t *)node->impl;
    if (!dev || !dev->write) return 0;
    return blkdev_write_bytes(dev, offset, size, buffer);
}

void blkdev_register(blkdev_t *dev) {
    if (!dev) return;
    
    // Setup VFS node
    memset(&dev->node, 0, sizeof(fs_node_t));
    strncpy(dev->node.name, dev->name, sizeof(dev->node.name) - 1);
    dev->node.flags = FS_BLOCKDEVICE;
    dev->node.length = dev->total_sectors * dev->sector_size;
    dev->node.impl = (uint32_t)(uintptr_t)dev;
    dev->node.read = blkdev_vfs_read;
    dev->node.write = blkdev_vfs_write;
    
    // Register with DevFS
    devfs_register_device(&dev->node);
    
    // Add to list
    dev->next = blkdev_list;
    blkdev_list = dev;
    
    kprint("Block device /dev/storage/");
    kprint(dev->name);
    kprint(" registered\n");
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
    return provider->blkdev->write(provider->blkdev, sector, (uint32_t)count, buf);
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
    
    uint32_t sector_size = dev->sector_size;
    uint64_t start_sector = offset / sector_size;
    uint32_t sector_offset = offset % sector_size;
    size_t total_read = 0;
    uint8_t *buf = (uint8_t *)buffer;
    
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

        if (dev->read(dev, start_sector, 1, sector_buf) != 0) {
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
    if (size >= sector_size) {
        uint32_t sector_count = size / sector_size;

        // Read directly into user buffer
        if (dev->read(dev, start_sector, sector_count, buf) != 0) {
            if (sector_buf && sector_buf != stack_buf) kfree(sector_buf, sector_size);
            return total_read;
        }

        uint32_t bytes_read = sector_count * sector_size;
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

        if (dev->read(dev, start_sector, 1, sector_buf) != 0) {
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
    
    uint32_t sector_size = dev->sector_size;
    uint64_t start_sector = offset / sector_size;
    uint32_t sector_offset = offset % sector_size;
    size_t total_written = 0;
    const uint8_t *buf = (const uint8_t *)buffer;
    
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

        uint32_t copy_size = sector_size - sector_offset;
        if (copy_size > size) copy_size = size;
        
        // Read-Modify-Write
        if (dev->read && dev->read(dev, start_sector, 1, sector_buf) != 0) {
            if (sector_buf && sector_buf != stack_buf) kfree(sector_buf, sector_size);
            return 0;
        }
        
        memcpy(sector_buf + sector_offset, buf, copy_size);
        
        if (dev->write(dev, start_sector, 1, sector_buf) != 0) {
            if (sector_buf && sector_buf != stack_buf) kfree(sector_buf, sector_size);
            return 0;
        }
        
        buf += copy_size;
        total_written += copy_size;
        size -= copy_size;
        start_sector++;
    }
    
    // 2. Handle aligned full sectors (Bulk Write)
    if (size >= sector_size) {
        uint32_t sector_count = size / sector_size;

        // Write directly from user buffer
        if (dev->write(dev, start_sector, sector_count, buf) != 0) {
            if (sector_buf && sector_buf != stack_buf) kfree(sector_buf, sector_size);
            return total_written;
        }

        uint32_t bytes_written = sector_count * sector_size;
        buf += bytes_written;
        total_written += bytes_written;
        size -= bytes_written;
        start_sector += sector_count;
    }

    // 3. Handle unaligned end (tail)
    if (size > 0) {
        if (!sector_buf) {
            if (sector_size <= STACK_BUF_SIZE) {
                sector_buf = stack_buf;
            } else {
                sector_buf = kmalloc(sector_size);
                if (!sector_buf) return total_written;
            }
        }

        // Read-Modify-Write
        if (dev->read && dev->read(dev, start_sector, 1, sector_buf) != 0) {
            if (sector_buf && sector_buf != stack_buf) kfree(sector_buf, sector_size);
            return total_written;
        }

        memcpy(sector_buf, buf, size);

        if (dev->write(dev, start_sector, 1, sector_buf) != 0) {
            if (sector_buf && sector_buf != stack_buf) kfree(sector_buf, sector_size);
            return total_written;
        }

        total_written += size;
    }

    if (sector_buf && sector_buf != stack_buf) kfree(sector_buf, sector_size);
    return total_written;
}
