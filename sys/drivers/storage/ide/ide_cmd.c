/*
 * ide_cmd.c - Drive selection and command issue helpers.
 */

#include <drivers/storage/ide/ide.h>
#include <drivers/storage/ide/ide_priv.h>

/* Select drive on channel (0xA0 = master, 0xB0 = slave in LBA mode). */
void ide_select_drive(uint8_t channel, uint8_t drive) {
    ide_write_reg(channel, ATA_REG_DEVICE, 0xA0 | (drive << 4));
    ide_400ns(channel);
}

int ide_issue_non_data_command(uint8_t channel, uint8_t drive,
                               uint8_t command, const char *op) {
    uint8_t status;

    if (channel >= MAX_IDE_CHANNELS || drive > 1) {
        return -1;
    }

    ide_select_drive(channel, drive);
    status = ide_read_reg(channel, ATA_REG_STATUS);
    if (status == 0 || status == 0xFF) {
        return -1;
    }

    if (ide_wait_ready(channel, IDE_TIMEOUT_READY_MS, op) < 0) {
        return -1;
    }

    ide_write_reg(channel, ATA_REG_COMMAND, command);
    if (ide_wait_bsy(channel, IDE_TIMEOUT_READY_MS, op) < 0) {
        return -1;
    }

    status = ide_read_reg(channel, ATA_REG_STATUS);
    if ((status & (ATA_SR_ERR | ATA_SR_DF)) != 0) {
        return -1;
    }

    return 0;
}

/*
 * Issue a read/write command after ensuring the drive is ready.
 *
 * THE FIX: before programming the LBA registers and writing the command,
 * select the drive and wait for it to be ready (BSY clear AND DRDY set).
 * The previous PIO path issued the command unconditionally, so a write
 * arriving while the prior write was still BSY never saw DRQ and burned
 * its full timeout ("pio-write timeout waiting for DRQ status=50").
 *
 * Register programming after the readiness wait matches the historical
 * PIO ordering exactly (LBA28 vs LBA48).
 */
int ide_issue_rw(uint8_t channel, uint8_t drive, uint64_t lba,
                 uint16_t count, uint8_t command, int lba48,
                 const char *op) {
    if (channel >= MAX_IDE_CHANNELS || drive > 1) {
        return -1;
    }

    /* Ensure the drive is ready before touching the command block. */
    ide_select_drive(channel, drive);
    if (ide_wait_ready(channel, IDE_TIMEOUT_READY_MS, op) < 0) {
        return -1;
    }

    if (lba48) {
        ide_write_reg(channel, ATA_REG_DEVICE, 0x40 | (drive << 4));
        ide_write_reg(channel, ATA_REG_SEC_COUNT, (uint8_t)(count >> 8));
        ide_write_reg(channel, ATA_REG_LBA_LOW, (uint8_t)(lba >> 24));
        ide_write_reg(channel, ATA_REG_LBA_MID, (uint8_t)(lba >> 32));
        ide_write_reg(channel, ATA_REG_LBA_HIGH, (uint8_t)(lba >> 40));
        ide_write_reg(channel, ATA_REG_SEC_COUNT, (uint8_t)count);
        ide_write_reg(channel, ATA_REG_LBA_LOW, (uint8_t)lba);
        ide_write_reg(channel, ATA_REG_LBA_MID, (uint8_t)(lba >> 8));
        ide_write_reg(channel, ATA_REG_LBA_HIGH, (uint8_t)(lba >> 16));
    } else {
        ide_write_reg(channel, ATA_REG_DEVICE,
                      0xE0 | (drive << 4) | ((lba >> 24) & 0x0F));
        ide_write_reg(channel, ATA_REG_SEC_COUNT, (uint8_t)count);
        ide_write_reg(channel, ATA_REG_LBA_LOW, (uint8_t)lba);
        ide_write_reg(channel, ATA_REG_LBA_MID, (uint8_t)(lba >> 8));
        ide_write_reg(channel, ATA_REG_LBA_HIGH, (uint8_t)(lba >> 16));
    }

    ide_write_reg(channel, ATA_REG_COMMAND, command);
    return 0;
}
