#include "ide.h"
#include "../../video/vga.h"
#include "../../../arch/i386/io.h"

#define ATA_PRIMARY_IO   0x1F0
#define ATA_SECONDARY_IO 0x170

void ide_init(void) {
    vga_write("IDE Driver Initialized.\n", 24);
    
    // Simple check for floating bus
    uint8_t status = inb(ATA_PRIMARY_IO + 7);
    if (status == 0xFF) {
        vga_write("IDE: Primary bus floating.\n", 25);
    } else {
        vga_write("IDE: Primary bus present.\n", 24);
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

