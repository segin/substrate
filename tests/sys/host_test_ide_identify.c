#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <drivers/storage/ide/ide.h>

static void encode_ata_string(uint16_t *dst, size_t words, const char *src) {
    char padded[64];
    size_t bytes = words * 2;

    assert(bytes <= sizeof(padded));
    memset(padded, ' ', bytes);
    strncpy(padded, src, bytes);
    for (size_t i = 0; i < words; i++) {
        dst[i] = (uint16_t)(((uint8_t)padded[i * 2] << 8) |
                            (uint8_t)padded[i * 2 + 1]);
    }
}

static void test_parse_identify_ata(void) {
    uint16_t buf[256];
    ide_device_t dev;

    memset(buf, 0, sizeof(buf));
    buf[0] = 0x0040;
    buf[49] = (1U << 8);
    buf[60] = 0x5678;
    buf[61] = 0x1234;
    buf[63] = 0x0007;
    buf[76] = (1U << 8);
    buf[82] = (1U << 0);
    buf[83] = (1U << 10);
    buf[88] = 0x003F;
    buf[100] = 0x4444;
    buf[101] = 0x3333;
    buf[102] = 0x2222;
    buf[103] = 0x1111;
    buf[169] = 0x0001;

    encode_ata_string(buf + 10, 10, "SERIAL-123");
    encode_ata_string(buf + 23, 4, "FW1.0");
    encode_ata_string(buf + 27, 20, "Substrate ATA Disk");

    ide_parse_identify_data(&dev, buf, 0, 2, 1);

    assert(dev.present == 1);
    assert(dev.type == 0);
    assert(dev.channel == 2);
    assert(dev.drive == 1);
    assert(dev.signature == 0x0040);
    assert(strcmp(dev.serial, "SERIAL-123") == 0);
    assert(strcmp(dev.firmware, "FW1.0") == 0);
    assert(strcmp(dev.model, "Substrate ATA Disk") == 0);
    assert(dev.size == 0x1111222233334444ULL);
    assert((dev.feature_flags & IDE_FEATURE_DMA) != 0);
    assert((dev.feature_flags & IDE_FEATURE_LBA48) != 0);
    assert((dev.feature_flags & IDE_FEATURE_SMART) != 0);
    assert((dev.feature_flags & IDE_FEATURE_NCQ) != 0);
    assert((dev.feature_flags & IDE_FEATURE_TRIM) != 0);
    assert(dev.mwdma_modes == 0x07);
    assert(dev.udma_modes == 0x3F);
    assert(dev.dma_mode == 1);
    {
        uint8_t mode;

        assert(ide_select_dma_transfer_mode(&dev, &mode) == 0);
        assert(mode == (uint8_t)(ATA_XFER_MODE_UDMA_BASE + 5));
    }
}

static void test_parse_identify_ata_lba28_only(void) {
    uint16_t buf[256];
    ide_device_t dev;

    memset(buf, 0, sizeof(buf));
    buf[60] = 0x00AA;
    buf[61] = 0x0055;
    encode_ata_string(buf + 27, 20, "Legacy IDE");

    ide_parse_identify_data(&dev, buf, 0, 0, 0);

    assert(dev.size == 0x005500AAULL);
    assert(strcmp(dev.model, "Legacy IDE") == 0);
    assert(dev.dma_mode == 0);
    assert(dev.udma_modes == 0);
    assert(dev.mwdma_modes == 0);
    assert((dev.feature_flags & IDE_FEATURE_LBA48) == 0);
    {
        uint8_t mode = 0xFF;

        assert(ide_select_dma_transfer_mode(&dev, &mode) < 0);
        assert(mode == 0xFF);
    }
}

static void test_parse_identify_atapi(void) {
    uint16_t buf[256];
    ide_device_t dev;

    memset(buf, 0, sizeof(buf));
    encode_ata_string(buf + 27, 20, "ATAPI CDROM");
    ide_parse_identify_data(&dev, buf, 1, 3, 0);

    assert(dev.type == 1);
    assert(strcmp(dev.model, "ATAPI CDROM") == 0);
    assert(dev.size == 0);
}

static void test_decode_error_bits(void) {
    char buf[64];

    ide_decode_error((uint8_t)(ATA_ER_UNC | ATA_ER_ABRT | ATA_ER_IDNF),
                     buf, sizeof(buf));
    assert(strstr(buf, "UNC") != NULL);
    assert(strstr(buf, "ABRT") != NULL);
    assert(strstr(buf, "IDNF") != NULL);

    ide_decode_error(0, buf, sizeof(buf));
    assert(strcmp(buf, "none") == 0);
}

static void test_channel_index_from_io(void) {
    uint8_t channel = 0xFF;

    assert(ide_channel_index_from_io(ATA_PRIMARY_IO, &channel) == 0);
    assert(channel == 0);
    assert(ide_channel_index_from_io(ATA_SECONDARY_IO, &channel) == 0);
    assert(channel == 1);
    assert(ide_channel_index_from_io(ATA_TERTIARY_IO, &channel) == 0);
    assert(channel == 2);
    assert(ide_channel_index_from_io(ATA_QUATERNARY_IO, &channel) == 0);
    assert(channel == 3);
    assert(ide_channel_index_from_io(0x1234, &channel) < 0);
    assert(ide_channel_index_from_io(ATA_PRIMARY_IO, NULL) < 0);
}

int main(void) {
    assert(ATA_TERTIARY_IO == 0x1E8);
    assert(ATA_TERTIARY_CTRL == 0x3EE);
    assert(ATA_QUATERNARY_IO == 0x168);
    assert(ATA_QUATERNARY_CTRL == 0x36E);
    assert(MAX_IDE_CHANNELS == 4);
    assert(MAX_IDE_DEVICES == 8);
    assert(IDE_DEVICE_INDEX(0, 0) == 0);
    assert(IDE_DEVICE_INDEX(0, 1) == 1);
    assert(IDE_DEVICE_INDEX(1, 0) == 2);
    assert(IDE_DEVICE_INDEX(1, 1) == 3);
    assert(IDE_DEVICE_INDEX(2, 0) == 4);
    assert(IDE_DEVICE_INDEX(2, 1) == 5);
    assert(IDE_DEVICE_INDEX(3, 0) == 6);
    assert(IDE_DEVICE_INDEX(3, 1) == 7);

    test_parse_identify_ata();
    test_parse_identify_ata_lba28_only();
    test_parse_identify_atapi();
    test_decode_error_bits();
    test_channel_index_from_io();

    puts("host_test_ide_identify: PASS");
    return 0;
}
