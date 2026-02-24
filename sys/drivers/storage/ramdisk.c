#include <drivers/storage/blkdev.h>
#include <kern/console.h>
#include <string.h>
#include <stddef.h>

// RAM Disk Block Driver using blkdev abstraction

#define MAX_RAMDISKS 8

static blkdev_t ramdisks[MAX_RAMDISKS];
static int ramdisk_count = 0;

// Read callback
static int ramdisk_read(blkdev_t *dev, uint64_t sector, uint32_t count, void *buffer) {
    void *addr = dev->priv;
    if (!addr) return -1;
    
    if (sector >= dev->total_sectors) return -1;
    if (count > dev->total_sectors - sector) return -1;

    uint64_t offset = sector * 512;
    uint64_t size = count * 512;
    
    memcpy(buffer, (uint8_t *)addr + offset, size);
    return 0;
}

// Write callback
static int ramdisk_write(blkdev_t *dev, uint64_t sector, uint32_t count, const void *buffer) {
    void *addr = dev->priv;
    if (!addr) return -1;
    
    if (sector >= dev->total_sectors) return -1;
    if (count > dev->total_sectors - sector) return -1;

    uint64_t offset = sector * 512;
    uint64_t size = count * 512;
    
    memcpy((uint8_t *)addr + offset, buffer, size);
    return 0;
}

// Create and register a RAM disk
int ramdisk_create(void *addr, size_t size) {
    if (!addr || size == 0) return -1;
    if (ramdisk_count >= MAX_RAMDISKS) return -1;
    
    blkdev_t *bdev = &ramdisks[ramdisk_count];
    memset(bdev, 0, sizeof(blkdev_t));
    
    // Name: ram0, ram1, etc.
    bdev->name[0] = 'r'; bdev->name[1] = 'a'; bdev->name[2] = 'm';
    bdev->name[3] = '0' + ramdisk_count;
    bdev->name[4] = '\0';
    
    bdev->sector_size = 512;
    bdev->total_sectors = size / 512;
    bdev->priv = addr;
    bdev->read = ramdisk_read;
    bdev->write = ramdisk_write;
    
    // Register with blkdev layer (auto-registers with DevFS)
    blkdev_register(bdev);
    
    ramdisk_count++;
    return ramdisk_count - 1;
}

// Legacy init function for compatibility with Multiboot module loading
void ramdisk_init(void *addr, size_t size) {
    ramdisk_create(addr, size);
}
