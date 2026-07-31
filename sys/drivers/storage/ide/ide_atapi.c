/*
 * ide_atapi.c - ATAPI packet command interface.
 *
 * ATAPI uses SCSI command packets sent over the ATA interface.  The
 * PACKET command (0xA0) is followed by a 12-byte CDB (Command Descriptor
 * Block) containing the SCSI command.
 */

#include <drivers/storage/ide/ide.h>
#include <drivers/storage/ide/ide_priv.h>

#include <arch/x86-common/io.h>

/* SCSI Command Codes (subset for CD-ROM) */
#define SCSI_TEST_UNIT_READY   0x00
#define SCSI_REQUEST_SENSE     0x03
#define SCSI_READ_6            0x08
#define SCSI_INQUIRY           0x12
#define SCSI_READ_CAPACITY     0x25
#define SCSI_READ_10           0x28
#define SCSI_READ_12           0xA8
#define SCSI_READ_TOC          0x43

/*
 * Send an ATAPI packet command
 *
 * Implements the ATAPI PIO protocol:
 * 1. Set byte count limit (transfer size)
 * 2. Issue PACKET command (0xA0)
 * 3. Wait for DRQ
 * 4. Send 12-byte CDB
 * 5. For data transfers: wait for DRQ, read/write data
 * 6. Check status
 */
int ide_atapi_packet(uint8_t channel, uint8_t drive,
                     const uint8_t *packet, uint8_t packet_len,
                     void *buffer, uint32_t buffer_len, int write) {
    if (channel >= MAX_IDE_CHANNELS) return -1;
    if (packet_len != 12) return -1;  /* ATAPI uses 12-byte CDB */

    uint16_t bus = ide_channels[channel].io_base;

    /* Select drive */
    ide_select_drive(channel, drive);
    if (ide_wait_ready_ex(channel, IDE_TIMEOUT_PACKET_MS, "packet-select", 0) < 0) return -1;

    /*
     * [IDE-07] The byte-count limit is a 16-bit field split across LBA_MID
     * and LBA_HIGH.  buffer_len is 32-bit and was written straight in with
     * `& 0xFF` / `>> 8 & 0xFF`, discarding bits 16 and up: a 256-sector CD
     * read-ahead (524288 bytes) programmed a BCL of 0x0000, which is
     * illegal, so the transfer failed spuriously -- and three of those drove
     * ide_mark_offline() and reset the channel.  Clamp to the largest legal
     * even value instead of silently truncating.
     */
    uint32_t bcl = buffer_len;
    if (bcl > 0xFFFEU)
        bcl = 0xFFFEU;
    bcl &= ~1U;                     /* BCL must be even */
    if (bcl == 0)
        return -1;                  /* a zero limit is not expressible */

    ide_write_reg(channel, ATA_REG_LBA_MID, bcl & 0xFF);
    ide_write_reg(channel, ATA_REG_LBA_HIGH, (bcl >> 8) & 0xFF);

    /* Issue PACKET command */
    ide_write_reg(channel, ATA_REG_COMMAND, ATA_CMD_PACKET);

    /* [IDE-17] The command register write needs the 400 ns settle before
     * STATUS is meaningful; without it the first read can still show the
     * pre-command state and the ATAPI path returns a spurious error. */
    ide_400ns(channel);

    /* Wait for DRQ to send the command packet */
    if (ide_wait_bsy(channel, IDE_TIMEOUT_PACKET_MS, "packet-command") < 0) {
        return -1;
    }

    uint8_t status = ide_read_reg(channel, ATA_REG_STATUS);
    if (status & ATA_SR_ERR) {
        return -1;
    }
    if (!(status & ATA_SR_DRQ)) {
        return -1;
    }

    /* Send the 12-byte CDB as 6 words */
    const uint16_t *pkt = (const uint16_t *)packet;
    for (int i = 0; i < 6; i++) {
        outw(bus + ATA_REG_DATA, pkt[i]);
    }

    /* For non-data commands, we're done */
    if (buffer_len == 0 || buffer == NULL) {
        if (ide_wait_bsy(channel, IDE_TIMEOUT_PACKET_MS, "packet-nodata") < 0) {
            return -1;
        }
        status = ide_read_reg(channel, ATA_REG_STATUS);
        return (status & ATA_SR_ERR) ? -1 : 0;
    }

    /* Data transfer phase */
    uint16_t *buf = (uint16_t *)buffer;
    uint32_t transferred = 0;

    while (transferred < buffer_len) {
        /* Wait for DRQ or completion */
        if (ide_wait_bsy(channel, IDE_TIMEOUT_PACKET_MS, "packet-data") < 0) {
            return -1;
        }
        status = ide_read_reg(channel, ATA_REG_STATUS);

        if (status & ATA_SR_ERR) {
            return -1;
        }

        if (!(status & ATA_SR_DRQ)) {
            /* No more data */
            break;
        }

        /* Get transfer size from byte count */
        uint16_t byte_count = ide_read_reg(channel, ATA_REG_LBA_MID) |
                             (ide_read_reg(channel, ATA_REG_LBA_HIGH) << 8);

        /* Transfer data */
        uint16_t words = byte_count / 2;

        /* Ensure we don't overflow the buffer */
        if (transferred + words * 2 > buffer_len) {
             words = (buffer_len - transferred) / 2;
        }

        if (words > 0) {
            if (write) {
                outsw(bus + ATA_REG_DATA, buf, words);
            } else {
                insw(bus + ATA_REG_DATA, buf, words);
            }
            buf += words;
            transferred += words * 2;
        } else {
            /* No words to transfer in this chunk? (odd length/zero) */
            break;
        }
    }

    /* Wait for completion */
    if (ide_wait_bsy(channel, IDE_TIMEOUT_PACKET_MS, "packet-complete") < 0) {
        return -1;
    }
    status = ide_read_reg(channel, ATA_REG_STATUS);

    return (status & ATA_SR_ERR) ? -1 : 0;
}

/*
 * ATAPI Read Capacity command
 *
 * Returns the last LBA and block size of the media.
 */
int ide_atapi_read_capacity(uint8_t channel, uint8_t drive,
                            uint32_t *lba, uint32_t *block_size) {
    uint8_t packet[12] = {0};
    uint8_t response[8];

    packet[0] = SCSI_READ_CAPACITY;
    /* Rest are zeros */

    int ret = ide_atapi_packet(channel, drive, packet, 12,
                               response, 8, 0);
    if (ret < 0) return ret;

    /* Response is big-endian */
    *lba = ((uint32_t)response[0] << 24) |
           ((uint32_t)response[1] << 16) |
           ((uint32_t)response[2] << 8) |
           response[3];

    *block_size = ((uint32_t)response[4] << 24) |
                  ((uint32_t)response[5] << 16) |
                  ((uint32_t)response[6] << 8) |
                  response[7];

    return 0;
}

/*
 * ATAPI Read Sectors (READ10/READ12)
 *
 * Reads sectors from ATAPI device (CD-ROM).
 * Uses READ10 for small counts, READ12 for large.
 */
int ide_atapi_read_sectors(uint8_t channel, uint8_t drive,
                           uint32_t lba, uint16_t count, void *buffer) {
    return ide_atapi_read_sectors_ss(channel, drive, lba, count, buffer,
                                     ATAPI_DEFAULT_SECTOR_SIZE);
}

/*
 * IDE-02: the transfer length has to come from the device's OWN sector size.
 *
 * This used to compute `count * 2048` unconditionally while the probe path
 * (ide_refresh_device_slot) correctly takes the size from READ CAPACITY and
 * publishes it as blkdev.sector_size.  A device reporting anything other
 * than 2048 therefore had its block count multiplied by the wrong figure:
 * the caller sized its buffer from the negotiated size and this asked the
 * drive for a different number of bytes, so either the tail of every
 * transfer was dropped or the drive was told to write past the buffer.
 */
int ide_atapi_read_sectors_ss(uint8_t channel, uint8_t drive,
                              uint32_t lba, uint16_t count, void *buffer,
                              uint32_t sector_size) {
    uint8_t packet[12] = {0};

    if (sector_size == 0) {
        sector_size = ATAPI_DEFAULT_SECTOR_SIZE;
    }

    /* All uint16_t counts fit in READ10 (max 65535 sectors) */
    /* Use READ10 - 10-byte CDB */
    packet[0] = SCSI_READ_10;
    packet[1] = 0;
    /* LBA (big-endian) */
    packet[2] = (lba >> 24) & 0xFF;
    packet[3] = (lba >> 16) & 0xFF;
    packet[4] = (lba >> 8) & 0xFF;
    packet[5] = lba & 0xFF;
    /* Reserved */
    packet[6] = 0;
    /* Transfer length (big-endian) */
    packet[7] = (count >> 8) & 0xFF;
    packet[8] = count & 0xFF;
    packet[9] = 0;
    /* Remaining bytes are zeros */

    uint32_t buffer_len = (uint32_t)count * sector_size;

    return ide_atapi_packet(channel, drive, packet, 12,
                            buffer, buffer_len, 0);
}

/*
 * ATAPI Read TOC (Table of Contents)
 *
 * Reads the CD Table of Contents for audio CD support.
 */
int ide_atapi_read_toc(uint8_t channel, uint8_t drive,
                       uint8_t start_track, void *buffer, uint16_t buffer_len) {
    uint8_t packet[12] = {0};

    packet[0] = SCSI_READ_TOC;
    packet[1] = 0x02;  /* MSF format */
    packet[6] = start_track;
    packet[7] = (buffer_len >> 8) & 0xFF;
    packet[8] = buffer_len & 0xFF;

    return ide_atapi_packet(channel, drive, packet, 12,
                            buffer, buffer_len, 0);
}
