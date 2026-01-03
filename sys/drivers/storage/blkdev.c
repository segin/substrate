#include "blkdev.h"
#include "../../kern/console.h"
#include <string.h>

static blkdev_t *blkdev_list = NULL;

// VFS read wrapper - translates byte reads to sector reads
static uint32_t blkdev_vfs_read(fs_node_t *node, off_t offset, uint32_t size, uint8_t *buffer) {
    blkdev_t *dev = (blkdev_t *)node->impl;
    if (!dev || !dev->read) return 0;
    return blkdev_read_bytes(dev, offset, size, buffer);
}

// VFS write wrapper
static uint32_t blkdev_vfs_write(fs_node_t *node, off_t offset, uint32_t size, uint8_t *buffer) {
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

blkdev_t *blkdev_get(const char *name) {
    blkdev_t *dev = blkdev_list;
    while (dev) {
        if (strcmp(dev->name, name) == 0) return dev;
        dev = dev->next;
    }
    return NULL;
}

// Byte-oriented read - handles sector alignment
uint32_t blkdev_read_bytes(blkdev_t *dev, uint64_t offset, uint32_t size, void *buffer) {
    if (!dev || !dev->read || dev->sector_size == 0) return 0;
    
    uint32_t sector_size = dev->sector_size;
    // Use 32-bit division for i386 compatibility
    uint32_t offset32 = (uint32_t)offset;
    uint32_t start_sector = offset32 / sector_size;
    uint32_t sector_offset = offset32 % sector_size;
    uint32_t total_read = 0;
    uint8_t *buf = (uint8_t *)buffer;
    
    // Temporary sector buffer for unaligned reads
    static uint8_t sector_buf[512];
    
    while (size > 0) {
        // Read one sector
        if (dev->read(dev, start_sector, 1, sector_buf) != 0) {
            break; // Read error
        }
        
        // Calculate how much to copy from this sector
        uint32_t copy_size = sector_size - sector_offset;
        if (copy_size > size) copy_size = size;
        
        memcpy(buf, sector_buf + sector_offset, copy_size);
        
        buf += copy_size;
        total_read += copy_size;
        size -= copy_size;
        start_sector++;
        sector_offset = 0; // After first sector, always start at 0
    }
    
    return total_read;
}

// Byte-oriented write - handles sector alignment (read-modify-write for unaligned)
uint32_t blkdev_write_bytes(blkdev_t *dev, uint64_t offset, uint32_t size, const void *buffer) {
    if (!dev || !dev->write || dev->sector_size == 0) return 0;
    
    uint32_t sector_size = dev->sector_size;
    uint32_t offset32 = (uint32_t)offset;
    uint32_t start_sector = offset32 / sector_size;
    uint32_t sector_offset = offset32 % sector_size;
    uint32_t total_written = 0;
    const uint8_t *buf = (const uint8_t *)buffer;
    
    static uint8_t sector_buf[512];
    
    while (size > 0) {
        uint32_t copy_size = sector_size - sector_offset;
        if (copy_size > size) copy_size = size;
        
        // If not writing a full sector, need to read first
        if (sector_offset != 0 || copy_size != sector_size) {
            if (dev->read && dev->read(dev, start_sector, 1, sector_buf) != 0) {
                break;
            }
        }
        
        memcpy(sector_buf + sector_offset, buf, copy_size);
        
        if (dev->write(dev, start_sector, 1, sector_buf) != 0) {
            break;
        }
        
        buf += copy_size;
        total_written += copy_size;
        size -= copy_size;
        start_sector++;
        sector_offset = 0;
    }
    
    return total_written;
}
