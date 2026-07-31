#include <stdio.h>
#include <string.h>

#include <drivers/storage/ide/ide.h>

static void ide_copy_identify_string(char *dst, size_t dst_size,
                                     const uint16_t *src, size_t words) {
    size_t i;

    if (dst == NULL || dst_size == 0) {
        return;
    }

    memset(dst, 0, dst_size);
    for (i = 0; i < words && (i * 2 + 1) < dst_size; i++) {
        uint16_t word = src[i];

        dst[i * 2] = (char)((word >> 8) & 0xFF);
        dst[i * 2 + 1] = (char)(word & 0xFF);
    }

    for (i = dst_size; i > 0; i--) {
        char c = dst[i - 1];

        if (c == '\0' || c == ' ') {
            dst[i - 1] = '\0';
            continue;
        }
        break;
    }
}

static int ide_highest_mode_bit(uint8_t modes) {
    for (int bit = 7; bit >= 0; bit--) {
        if ((modes & (1U << bit)) != 0) {
            return bit;
        }
    }

    return -1;
}

void ide_parse_identify_data(ide_device_t *dev, const uint16_t *buffer,
                             uint8_t type, uint8_t channel, uint8_t drive) {
    uint64_t total_sectors = 0;
    uint32_t feature_flags = 0;
    uint8_t mwdma_modes = 0;
    uint8_t udma_modes = 0;

    if (dev == NULL || buffer == NULL) {
        return;
    }

    memset(dev, 0, sizeof(*dev));
    dev->present = 1;
    dev->type = type;
    dev->channel = channel;
    dev->drive = drive;
    dev->signature = buffer[0];
    dev->capabilities = buffer[49];
    dev->command_sets = (uint32_t)buffer[82] | ((uint32_t)buffer[83] << 16);

    ide_copy_identify_string(dev->serial, sizeof(dev->serial), buffer + 10, 10);
    ide_copy_identify_string(dev->firmware, sizeof(dev->firmware), buffer + 23, 4);
    ide_copy_identify_string(dev->model, sizeof(dev->model), buffer + 27, 20);

    if ((buffer[49] & (1U << 8)) != 0 || buffer[63] != 0 || buffer[88] != 0) {
        feature_flags |= IDE_FEATURE_DMA;
    }
    if ((buffer[82] & (1U << 0)) != 0) {
        feature_flags |= IDE_FEATURE_SMART;
    }
    if ((buffer[83] & (1U << 10)) != 0) {
        feature_flags |= IDE_FEATURE_LBA48;
    }
    if ((buffer[76] & (1U << 8)) != 0) {
        feature_flags |= IDE_FEATURE_NCQ;
    }
    if ((buffer[169] & 0x0001U) != 0) {
        feature_flags |= IDE_FEATURE_TRIM;
    }

    if (type == 0) {
        /*
         * [IDE-20] Word 83's contents are only meaningful when its validity
         * bits (15:14) read 01; on a floating bus every word reads 0xFFFF,
         * which sets bits 15:14 to 11 and made the LBA48 branch fire with
         * words 100-103 all 0xFFFF -- a reported capacity of
         * 0xFFFFFFFFFFFF sectors.  Require the validity pattern before
         * trusting the 48-bit capacity, and fall back to the 28-bit words.
         */
        int word83_valid = ((buffer[83] & 0xC000U) == 0x4000U);

        if ((feature_flags & IDE_FEATURE_LBA48) != 0 && word83_valid) {
            total_sectors = (uint64_t)buffer[100] |
                            ((uint64_t)buffer[101] << 16) |
                            ((uint64_t)buffer[102] << 32) |
                            ((uint64_t)buffer[103] << 48);
        } else {
            total_sectors = (uint64_t)buffer[60] |
                            ((uint64_t)buffer[61] << 16);
        }

        /* A 48-bit command cannot address beyond 2^48 sectors, and an
         * all-ones IDENTIFY must not be believed. */
        if (total_sectors >= (1ULL << 48)) {
            total_sectors = 0;
        }
    }

    mwdma_modes = (uint8_t)(buffer[63] & 0xFF);
    /*
     * [IDE-15] UDMA modes come from word 88, but modes above UDMA2 (i.e.
     * faster than 33 MB/s) require an 80-conductor cable, which word 93
     * bit 13 reports.  Selecting UDMA5 across a 40-conductor cable does not
     * fail cleanly -- it produces intermittent CRC-corrupted reads, which is
     * far worse than running slower.  Also mask off bit 7, which is reserved
     * (there is no UDMA7).
     */
    udma_modes = (uint8_t)(buffer[88] & 0x7F);

    if ((buffer[93] & (1U << 13)) == 0) {
        /* 40-conductor cable (or word 93 not reported): cap at UDMA2. */
        udma_modes &= 0x07;
    }

    dev->size = total_sectors;
    dev->feature_flags = feature_flags;
    dev->mwdma_modes = mwdma_modes;
    dev->udma_modes = udma_modes;
    if (udma_modes != 0) {
        dev->dma_mode = 1;
    } else if (mwdma_modes != 0) {
        dev->dma_mode = 2;
    } else {
        dev->dma_mode = 0;
    }
}

size_t ide_decode_error(uint8_t error, char *buf, size_t size) {
    static const struct {
        uint8_t bit;
        const char *name;
    } ide_error_bits[] = {
        { ATA_ER_BBK, "BBK" },
        { ATA_ER_UNC, "UNC" },
        { ATA_ER_MC, "MC" },
        { ATA_ER_IDNF, "IDNF" },
        { ATA_ER_MCR, "MCR" },
        { ATA_ER_ABRT, "ABRT" },
        { ATA_ER_TK0NF, "TK0NF" },
        { ATA_ER_AMNF, "AMNF" },
    };
    size_t off = 0;
    size_t i;
    int first = 1;

    if (buf != NULL && size > 0) {
        buf[0] = '\0';
    }

    for (i = 0; i < sizeof(ide_error_bits) / sizeof(ide_error_bits[0]); i++) {
        int ret;

        if ((error & ide_error_bits[i].bit) == 0) {
            continue;
        }

        ret = snprintf(off < size ? buf + off : NULL,
                       off < size ? size - off : 0,
                       "%s%s",
                       first ? "" : "|",
                       ide_error_bits[i].name);
        if (ret > 0) {
            off += (size_t)ret;
        }
        first = 0;
    }

    if (first) {
        int ret = snprintf(buf, size, "none");
        if (ret > 0) {
            off = (size_t)ret;
        }
    }

    return off;
}

int ide_select_dma_transfer_mode(const ide_device_t *dev, uint8_t *mode) {
    int bit;

    if (dev == NULL || mode == NULL) {
        return -1;
    }

    bit = ide_highest_mode_bit(dev->udma_modes);
    if (bit >= 0) {
        *mode = (uint8_t)(ATA_XFER_MODE_UDMA_BASE + bit);
        return 0;
    }

    bit = ide_highest_mode_bit(dev->mwdma_modes);
    if (bit >= 0) {
        *mode = (uint8_t)(ATA_XFER_MODE_MWDMA_BASE + bit);
        return 0;
    }

    return -1;
}
