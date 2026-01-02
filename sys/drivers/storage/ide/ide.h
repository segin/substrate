#ifndef _IDE_H
#define _IDE_H

#include <stdint.h>

#define ATA_REG_DATA       0x00
#define ATA_REG_ERROR      0x01
#define ATA_REG_FEATURES   0x01
#define ATA_REG_SEC_COUNT  0x02
#define ATA_REG_LBA_LOW    0x03
#define ATA_REG_LBA_MID    0x04
#define ATA_REG_LBA_HIGH   0x05
#define ATA_REG_DEVICE     0x06
#define ATA_REG_STATUS     0x07
#define ATA_REG_COMMAND    0x07

#define ATA_CMD_READ_PIO   0x20
#define ATA_CMD_WRITE_PIO  0x30
#define ATA_CMD_READ_PIO_EXT 0x24
#define ATA_CMD_WRITE_PIO_EXT 0x34
#define ATA_CMD_READ_DMA_EXT 0x25
#define ATA_CMD_WRITE_DMA_EXT 0x35
#define ATA_CMD_IDENTIFY   0xEC

#define ATA_SR_BSY         0x80
#define ATA_SR_DRDY        0x40
#define ATA_SR_DF          0x20
#define ATA_SR_DSC         0x10
#define ATA_SR_DRQ         0x08
#define ATA_SR_CORR        0x04
#define ATA_SR_IDX         0x02
#define ATA_SR_ERR         0x01

void ide_init(void);
int ide_read_sectors(uint16_t bus, uint8_t drive, uint32_t lba, uint8_t count, void *buffer);
int ide_read_sectors_ext(uint16_t bus, uint8_t drive, uint64_t lba, uint16_t count, void *buffer);
int ide_write_sectors(uint16_t bus, uint8_t drive, uint32_t lba, uint8_t count, const void *buffer);
int ide_identify(uint16_t bus, uint8_t drive, void *buffer);
int ide_dma_setup(uint16_t bus, uint8_t drive, uint64_t lba, uint16_t count, void *phys_addr, int write);

#endif
