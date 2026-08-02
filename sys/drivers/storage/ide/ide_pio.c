/*
 * ide_pio.c - Programmed I/O (PIO) sector transfers, LBA28 and LBA48.
 */

#include <drivers/storage/ide/ide.h>
#include <drivers/storage/ide/ide_priv.h>

#include <arch/x86-common/io.h>

int ide_read_sectors(uint16_t bus, uint8_t drive, uint32_t lba,
                     uint16_t count, void *buffer) {
    uint8_t channel;

    if (ide_channel_index_from_io(bus, &channel) < 0) {
        return -1;
    }
    if (count == 0 || count > 256) {
        return -1;
    }

    /* ATA encodes a 256-sector transfer as a sector-count register of 0.
     * ide_issue_rw() writes the low byte, so passing 256 through gives the
     * right register value -- but the data loop below must iterate the true
     * count.  Narrowing to uint8_t before the loop made it run zero times
     * while the drive still transferred 128 KiB: the caller got a success
     * return with its buffer untouched, and the drive was left with DRQ
     * asserted, desynchronising the channel. */
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
                      uint16_t count, const void *buffer) {
    uint8_t channel;

    if (ide_channel_index_from_io(bus, &channel) < 0) {
        return -1;
    }
    if (count == 0 || count > 256) {
        return -1;
    }

    /* See ide_read_sectors(): 256 is encoded as a sector-count register of 0,
     * so the loop below must iterate the true count.  Narrowing beforehand
     * made the write report success having sent nothing. */
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
