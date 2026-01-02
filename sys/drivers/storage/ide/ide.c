#include "ide.h"
#include "ide.h"
#include "../../../arch/i386/io.h"
#include <string.h>
#include "../../../kern/console.h"
#include "../../../kern/geom/geom.h"

// Wrapper prototype
static int ide_read_sector_wrapper_geom(geom_disk_t *disk, uint64_t lba, size_t count, void *buf);

#define ATA_PRIMARY_IO   0x1F0
#define ATA_SECONDARY_IO 0x170

#define ATA_TERTIARY_IO   0x1E8
#define ATA_QUATERNARY_IO 0x168

void ide_init(void) {
    kprint("IDE Driver Initialized.\n");
    
    // Scan all 4 possible buses (8 drives max)
    // Primary (0x1F0)
    // Secondary (0x170)
    // Tertiary (0x1E8)
    // Quaternary (0x168)
    
    uint16_t buses[4] = { ATA_PRIMARY_IO, ATA_SECONDARY_IO, ATA_TERTIARY_IO, ATA_QUATERNARY_IO };
    const char *bus_names[4] = { "Primary", "Secondary", "Tertiary", "Quaternary" };
    const char *drive_names[2] = { "Master", "Slave" };

    for (int b = 0; b < 4; b++) {
        // Check for floating bus
        // If bus is floating, it usually returns 0xFF
        if (inb(buses[b] + 7) == 0xFF) continue;

        for (int d = 0; d < 2; d++) {
            uint16_t buf[256];
            memset(buf, 0, 512);
            int ret = ide_identify(buses[b], d, buf);
            if (ret == 0) {
                // Found a drive!
                char model[41];
                for(int i=0; i<20; i++) {
                    uint16_t w = buf[27+i];
                    model[i*2] = (w >> 8) & 0xFF; // ATA strings are big-endian
                    model[i*2+1] = w & 0xFF;
                }
                model[40] = 0;
                
                // Trim trailing spaces
                for(int i=39; i>=0; i--) {
                    if(model[i] == ' ') model[i] = 0;
                    else break;
                }

                // Canonical Device Name
                int dev_id = b * 2 + d;
                char id_str[4] = { '0' + dev_id, 0 }; // Works for 0-9
                
                // Helper to construct full name e.g. "ide0"
                // Ideally we would snprintf, but we build it manually or use id_str
                // We'll pass "ideX" to scanner.
                char dev_name[8];
                dev_name[0] = 'i'; dev_name[1] = 'd'; dev_name[2] = 'e';
                dev_name[3] = id_str[0];
                dev_name[4] = 0;
                
                kprint("Found device /dev/storage/");
                kprint(dev_name);
                kprint(": ");
                kprint(model);
                kprint(" (");
                kprint(bus_names[b]);
                kprint(" ");
                kprint(drive_names[d]);
                kprint(")\n");
                
                // Scan Partitions via GEOM
                // We need a context struct to pass bus/drive to read_func
                // But priv is void*, so we can malloc or use a static/stack struct if synchronous.
                // Since this is single threaded init, stack struct is fine.
                struct ide_disk_ctx {
                    uint16_t bus;
                    uint8_t drive;
                } ctx = { buses[b], d };
                
                geom_disk_t disk; // Stack allocated, dangerous if async!
                // But GEOM scan is currently synchronous. 
                // In real OS, we need heap or static array in ide.c for persistence.
                
                disk.name = dev_name;
                disk.priv = &ctx;
                disk.read = ide_read_sector_wrapper_geom;
                disk.write = NULL;
                disk.sector_size = 512;
                disk.media_size = 0; // Unknown/TODO
                
                geom_register_disk(&disk);
            }
        }
    }
}

// Wrapper for GEOM
static int ide_read_sector_wrapper_geom(geom_disk_t *disk, uint64_t lba, size_t count, void *buf) {
    struct ide_disk_ctx {
        uint16_t bus;
        uint8_t drive;
    } *ctx = disk->priv;
    // Read sectors
    // Note: ide_read_sectors takes uint32_t lba, so 2TB limit.
    return ide_read_sectors(ctx->bus, ctx->drive, (uint32_t)lba, (uint8_t)count, buf);
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
    outb(bus + ATA_REG_DEVICE, 0x40 | (drive << 4)); // LBA mode
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

int ide_dma_setup(uint16_t bus, uint8_t drive, uint64_t lba, uint16_t count, void *phys_addr, int write) {
    // 1. Setup PRDT (Physical Region Descriptor Table)
    // 2. Write PRDT address to Bus Master IDE register
    // 3. Set Device register (LBA48)
    // 4. Send command (ATA_CMD_READ_DMA_EXT or WRITE)
    // 5. Start DMA in BMIDE Status register
    (void)bus; (void)drive; (void)lba; (void)count; (void)phys_addr; (void)write;
    return 0; // Stub
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

int ide_identify(uint16_t bus, uint8_t drive, void *buffer) {
    outb(bus + ATA_REG_DEVICE, 0xA0 | (drive << 4));
    outb(bus + ATA_REG_SEC_COUNT, 0);
    outb(bus + ATA_REG_LBA_LOW, 0);
    outb(bus + ATA_REG_LBA_MID, 0);
    outb(bus + ATA_REG_LBA_HIGH, 0);
    outb(bus + ATA_REG_COMMAND, ATA_CMD_IDENTIFY);

    uint8_t status = inb(bus + ATA_REG_STATUS);
    if (status == 0) return -1; // Drive doesn't exist

    ide_wait_bsy(bus);
    
    // Check if not ATA (ATAPI check)
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

