/*
 * ide_pio.c - Programmed I/O (PIO) sector transfers, LBA28 and LBA48.
 */

#include <drivers/storage/ide/ide.h>
#include <drivers/storage/ide/ide_priv.h>

#include <arch/x86-common/io.h>

int ide_read_sectors(uint16_t bus, uint8_t drive, uint32_t lba,
                     uint8_t count, void *buffer) {
    uint8_t channel;

    if (ide_channel_index_from_io(bus, &channel) < 0) {
        return -1;
    }

    if (ide_issue_rw(channel, drive, lba, count, ATA_CMD_READ_PIO, 0,
                     "pio-read") < 0) {
        return -1;
    }

    uint16_t *buf = (uint16_t *)buffer;
    for (int i = 0; i < count; i++) {
        if (ide_wait_bsy(channel, IDE_TIMEOUT_DATA_MS, "pio-read") < 0) {
            return -1;
        }
        if (ide_wait_drq(channel, IDE_TIMEOUT_DATA_MS, "pio-read") < 0) {
            return -1;
        }
        insw(bus + ATA_REG_DATA, buf, 256);
        buf += 256;
    }
    return 0;
}

int ide_read_sectors_ext(uint16_t bus, uint8_t drive, uint64_t lba,
                         uint16_t count, void *buffer) {
    uint8_t channel;

    if (ide_channel_index_from_io(bus, &channel) < 0) {
        return -1;
    }

    if (ide_issue_rw(channel, drive, lba, count, ATA_CMD_READ_PIO_EXT, 1,
                     "pio-read-ext") < 0) {
        return -1;
    }

    uint16_t *buf = (uint16_t *)buffer;
    for (int i = 0; i < count; i++) {
        if (ide_wait_bsy(channel, IDE_TIMEOUT_DATA_MS, "pio-read-ext") < 0) {
            return -1;
        }
        if (ide_wait_drq(channel, IDE_TIMEOUT_DATA_MS, "pio-read-ext") < 0) {
            return -1;
        }
        insw(bus + ATA_REG_DATA, buf, 256);
        buf += 256;
    }
    return 0;
}

int ide_write_sectors(uint16_t bus, uint8_t drive, uint32_t lba,
                      uint8_t count, const void *buffer) {
    uint8_t channel;

    if (ide_channel_index_from_io(bus, &channel) < 0) {
        return -1;
    }

    if (ide_issue_rw(channel, drive, lba, count, ATA_CMD_WRITE_PIO, 0,
                     "pio-write") < 0) {
        return -1;
    }

    const uint16_t *buf = (const uint16_t *)buffer;
    for (int i = 0; i < count; i++) {
        if (ide_wait_bsy(channel, IDE_TIMEOUT_DATA_MS, "pio-write") < 0) {
            return -1;
        }
        if (ide_wait_drq(channel, IDE_TIMEOUT_DATA_MS, "pio-write") < 0) {
            return -1;
        }
        outsw(bus + ATA_REG_DATA, buf, 256);
        buf += 256;
    }

    /*
     * Let the drive flush the last sector and return to a ready state
     * before we hand control back, so the next op starts from a clean
     * (BSY clear, DRDY set) drive instead of racing a still-busy write.
     */
    if (ide_wait_ready(channel, IDE_TIMEOUT_DATA_MS, "pio-write-complete") < 0) {
        return -1;
    }
    return 0;
}

int ide_write_sectors_ext(uint16_t bus, uint8_t drive, uint64_t lba,
                          uint16_t count, const void *buffer) {
    uint8_t channel;

    if (ide_channel_index_from_io(bus, &channel) < 0) {
        return -1;
    }

    if (ide_issue_rw(channel, drive, lba, count, ATA_CMD_WRITE_PIO_EXT, 1,
                     "pio-write-ext") < 0) {
        return -1;
    }

    const uint16_t *buf = (const uint16_t *)buffer;
    for (int i = 0; i < count; i++) {
        if (ide_wait_bsy(channel, IDE_TIMEOUT_DATA_MS, "pio-write-ext") < 0) {
            return -1;
        }
        if (ide_wait_drq(channel, IDE_TIMEOUT_DATA_MS, "pio-write-ext") < 0) {
            return -1;
        }
        outsw(bus + ATA_REG_DATA, buf, 256);
        buf += 256;
    }

    if (ide_wait_ready(channel, IDE_TIMEOUT_DATA_MS, "pio-write-ext-complete") < 0) {
        return -1;
    }
    return 0;
}
