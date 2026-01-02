#include "ide.h"
#include "../../../arch/i386/io.h"
#include <string.h>
#include "../../../kern/console.h"
#include "../blkdev.h"

#define ATA_PRIMARY_IO   0x1F0
#define ATA_SECONDARY_IO 0x170
#define ATA_TERTIARY_IO   0x1E8
#define ATA_QUATERNARY_IO 0x168

#define MAX_IDE_DRIVES 8

// Per-drive context stored with blkdev
typedef struct {
    uint16_t bus;
    uint8_t drive;
} ide_drive_ctx_t;

static ide_drive_ctx_t ide_contexts[MAX_IDE_DRIVES];
static blkdev_t ide_devices[MAX_IDE_DRIVES];
static int ide_device_count = 0;

// Forward declarations
static void ide_wait_bsy(uint16_t bus);
static void ide_wait_drq(uint16_t bus);

// Block device read callback
static int ide_blkdev_read(blkdev_t *dev, uint64_t sector, uint32_t count, void *buffer) {
    ide_drive_ctx_t *ctx = (ide_drive_ctx_t *)dev->priv;
    return ide_read_sectors(ctx->bus, ctx->drive, (uint32_t)sector, (uint8_t)count, buffer);
}

// Block device write callback
static int ide_blkdev_write(blkdev_t *dev, uint64_t sector, uint32_t count, const void *buffer) {
    ide_drive_ctx_t *ctx = (ide_drive_ctx_t *)dev->priv;
    return ide_write_sectors(ctx->bus, ctx->drive, (uint32_t)sector, (uint8_t)count, buffer);
}

void ide_init(void) {
    kprint("IDE Driver Initialized.\n");
    
    uint16_t buses[4] = { ATA_PRIMARY_IO, ATA_SECONDARY_IO, ATA_TERTIARY_IO, ATA_QUATERNARY_IO };
    const char *bus_names[4] = { "Primary", "Secondary", "Tertiary", "Quaternary" };
    const char *drive_names[2] = { "Master", "Slave" };

    for (int b = 0; b < 4; b++) {
        // Check for floating bus
        if (inb(buses[b] + 7) == 0xFF) continue;

        for (int d = 0; d < 2; d++) {
            uint16_t buf[256];
            memset(buf, 0, 512);
            int ret = ide_identify(buses[b], d, buf);
            if (ret == 0 && ide_device_count < MAX_IDE_DRIVES) {
                // Found a drive!
                char model[41];
                for(int i=0; i<20; i++) {
                    uint16_t w = buf[27+i];
                    model[i*2] = (w >> 8) & 0xFF;
                    model[i*2+1] = w & 0xFF;
                }
                model[40] = 0;
                
                // Trim trailing spaces
                for(int i=39; i>=0; i--) {
                    if(model[i] == ' ') model[i] = 0;
                    else break;
                }

                // Get total sectors (LBA28)
                uint32_t total_sectors = buf[60] | ((uint32_t)buf[61] << 16);

                // Setup context
                ide_contexts[ide_device_count].bus = buses[b];
                ide_contexts[ide_device_count].drive = d;
                
                // Setup blkdev
                blkdev_t *bdev = &ide_devices[ide_device_count];
                memset(bdev, 0, sizeof(blkdev_t));
                
                // Name: ide0, ide1, etc.
                bdev->name[0] = 'i'; bdev->name[1] = 'd'; bdev->name[2] = 'e';
                bdev->name[3] = '0' + ide_device_count;
                bdev->name[4] = '\0';
                
                bdev->sector_size = 512;
                bdev->total_sectors = total_sectors;
                bdev->priv = &ide_contexts[ide_device_count];
                bdev->read = ide_blkdev_read;
                bdev->write = ide_blkdev_write;
                
                // Register with blkdev layer (auto-registers with DevFS)
                blkdev_register(bdev);
                
                kprint("  ");
                kprint(bdev->name);
                kprint(": ");
                kprint(model);
                kprint(" (");
                kprint(bus_names[b]);
                kprint(" ");
                kprint(drive_names[d]);
                kprint(")\n");
                
                ide_device_count++;
            }
        }
    }
}

static void ide_wait_bsy(uint16_t bus) {
    while (inb(bus + ATA_REG_STATUS) & ATA_SR_BSY);
}

static void ide_wait_drq(uint16_t bus) {
    while (!(inb(bus + ATA_REG_STATUS) & ATA_SR_DRQ));
}

int ide_read_sectors(uint16_t bus, uint8_t drive, uint32_t lba, uint8_t count, void *buffer) {
    outb(bus + ATA_REG_DEVICE, 0xE0 | (drive << 4) | ((lba >> 24) & 0x0F));
    outb(bus + ATA_REG_SEC_COUNT, count);
    outb(bus + ATA_REG_LBA_LOW, (uint8_t)lba);
    outb(bus + ATA_REG_LBA_MID, (uint8_t)(lba >> 8));
    outb(bus + ATA_REG_LBA_HIGH, (uint8_t)(lba >> 16));
    outb(bus + ATA_REG_COMMAND, ATA_CMD_READ_PIO);

    uint16_t *buf = (uint16_t *)buffer;
    for (int i = 0; i < count; i++) {
        ide_wait_bsy(bus);
        ide_wait_drq(bus);
        for (int j = 0; j < 256; j++) {
            *buf++ = inw(bus + ATA_REG_DATA);
        }
    }
    return 0;
}

int ide_read_sectors_ext(uint16_t bus, uint8_t drive, uint64_t lba, uint16_t count, void *buffer) {
    outb(bus + ATA_REG_DEVICE, 0x40 | (drive << 4));
    outb(bus + ATA_REG_SEC_COUNT, (uint8_t)(count >> 8));
    outb(bus + ATA_REG_LBA_LOW, (uint8_t)(lba >> 24));
    outb(bus + ATA_REG_LBA_MID, (uint8_t)(lba >> 32));
    outb(bus + ATA_REG_LBA_HIGH, (uint8_t)(lba >> 40));
    outb(bus + ATA_REG_SEC_COUNT, (uint8_t)count);
    outb(bus + ATA_REG_LBA_LOW, (uint8_t)lba);
    outb(bus + ATA_REG_LBA_MID, (uint8_t)(lba >> 8));
    outb(bus + ATA_REG_LBA_HIGH, (uint8_t)(lba >> 16));
    outb(bus + ATA_REG_COMMAND, ATA_CMD_READ_PIO_EXT);

    uint16_t *buf = (uint16_t *)buffer;
    for (int i = 0; i < count; i++) {
        ide_wait_bsy(bus);
        ide_wait_drq(bus);
        for (int j = 0; j < 256; j++) {
            *buf++ = inw(bus + ATA_REG_DATA);
        }
    }
    return 0;
}

int ide_write_sectors(uint16_t bus, uint8_t drive, uint32_t lba, uint8_t count, const void *buffer) {
    outb(bus + ATA_REG_DEVICE, 0xE0 | (drive << 4) | ((lba >> 24) & 0x0F));
    outb(bus + ATA_REG_SEC_COUNT, count);
    outb(bus + ATA_REG_LBA_LOW, (uint8_t)lba);
    outb(bus + ATA_REG_LBA_MID, (uint8_t)(lba >> 8));
    outb(bus + ATA_REG_LBA_HIGH, (uint8_t)(lba >> 16));
    outb(bus + ATA_REG_COMMAND, ATA_CMD_WRITE_PIO);

    uint16_t *buf = (uint16_t *)buffer;
    for (int i = 0; i < count; i++) {
        ide_wait_bsy(bus);
        ide_wait_drq(bus);
        for (int j = 0; j < 256; j++) {
            outw(bus + ATA_REG_DATA, *buf++);
        }
    }
    return 0;
}

int ide_dma_setup(uint16_t bus, uint8_t drive, uint64_t lba, uint16_t count, void *phys_addr, int write) {
    (void)bus; (void)drive; (void)lba; (void)count; (void)phys_addr; (void)write;
    return 0; // Stub
}

int ide_identify(uint16_t bus, uint8_t drive, void *buffer) {
    outb(bus + ATA_REG_DEVICE, 0xA0 | (drive << 4));
    outb(bus + ATA_REG_SEC_COUNT, 0);
    outb(bus + ATA_REG_LBA_LOW, 0);
    outb(bus + ATA_REG_LBA_MID, 0);
    outb(bus + ATA_REG_LBA_HIGH, 0);
    outb(bus + ATA_REG_COMMAND, ATA_CMD_IDENTIFY);

    uint8_t status = inb(bus + ATA_REG_STATUS);
    if (status == 0) return -1;

    ide_wait_bsy(bus);
    
    if (inb(bus + ATA_REG_LBA_MID) != 0 || inb(bus + ATA_REG_LBA_HIGH) != 0) {
        return -2; // Not ATA
    }

    ide_wait_drq(bus);

    uint16_t *buf = (uint16_t *)buffer;
    for (int i = 0; i < 256; i++) {
        buf[i] = inw(bus + ATA_REG_DATA);
    }
    return 0;
}
