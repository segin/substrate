#ifndef _BLKDEV_H
#define _BLKDEV_H

#include <vfs/vfs.h>

// Block device structure
typedef struct blkdev {
    char name[32];              // Device name (e.g., "ide0", "ram0")
    uint32_t sector_size;       // Typically 512
    uint64_t total_sectors;     // Total sectors on device
    void *priv;                 // Driver-private data
    
    // Driver callbacks
    int (*read)(struct blkdev *dev, uint64_t sector, uint32_t count, void *buffer);
    int (*write)(struct blkdev *dev, uint64_t sector, uint32_t count, const void *buffer);
    
    // VFS integration
    fs_node_t node;             // fs_node for DevFS
    struct blkdev *next;        // Linked list
} blkdev_t;

// Register a block device (creates DevFS entry)
void blkdev_register(blkdev_t *dev);

// Get a block device by name
blkdev_t *blkdev_get(const char *name);

// Read from block device (byte-oriented wrapper)
uint32_t blkdev_read_bytes(blkdev_t *dev, uint64_t offset, uint32_t size, void *buffer);

// Write to block device (byte-oriented wrapper)
uint32_t blkdev_write_bytes(blkdev_t *dev, uint64_t offset, uint32_t size, const void *buffer);

#endif
