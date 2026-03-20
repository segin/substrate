#ifndef _FLOPPY_H
#define _FLOPPY_H

#include <stddef.h>
#include <stdint.h>

#define FDC_MAX_CONTROLLERS 2
#define FDC_MAX_DRIVES 4
#define FDC_DMA_CHANNEL 2
#define FDC_DMA_LIMIT 0x01000000U

#define FDC_PRIMARY_BASE 0x3F0
#define FDC_SECONDARY_BASE 0x370
#define FDC_PRIMARY_IRQ 6
#define FDC_SECONDARY_IRQ 10

#define FDC_DOR_OFFSET 0x02
#define FDC_MSR_OFFSET 0x04
#define FDC_FIFO_OFFSET 0x05
#define FDC_DIR_OFFSET 0x07
#define FDC_CCR_OFFSET 0x07

#define FDC_MSR_RQM 0x80
#define FDC_MSR_DIO 0x40
#define FDC_MSR_NDMA 0x20
#define FDC_MSR_BUSY_MASK 0x0F

#define FDC_DOR_DRIVE_MASK 0x03
#define FDC_DOR_RESET 0x04
#define FDC_DOR_DMA_IRQ 0x08
#define FDC_DOR_MOTOR0 0x10
#define FDC_DOR_MOTOR1 0x20
#define FDC_DOR_MOTOR2 0x40
#define FDC_DOR_MOTOR3 0x80

#define FDC_CMD_SPECIFY 0x03
#define FDC_CMD_SENSE_INTERRUPT 0x08
#define FDC_CMD_RECALIBRATE 0x07
#define FDC_CMD_SEEK 0x0F
#define FDC_CMD_CONFIGURE 0x13
#define FDC_CMD_READ_DATA 0x46
#define FDC_CMD_WRITE_DATA 0x45
#define FDC_CMD_FORMAT_TRACK 0x4D

#define FDC_ST0_IC_MASK 0xC0
#define FDC_ST0_IC_NORMAL 0x00
#define FDC_ST0_IC_ABNORMAL 0x40
#define FDC_ST0_IC_INVALID 0x80
#define FDC_ST0_IC_NOT_READY 0xC0

#define FDC_GAP3_HD 0x1B
#define FDC_GAP3_DD 0x2A
#define FDC_SECTOR_SHIFT_512 2

typedef struct fdc_geometry {
    uint8_t cmos_type;
    const char *name;
    uint16_t cylinders;
    uint8_t heads;
    uint8_t sectors_per_track;
    uint8_t data_rate;
} fdc_geometry_t;

typedef struct fdc_chs {
    uint16_t cylinder;
    uint8_t head;
    uint8_t sector;
} fdc_chs_t;

const fdc_geometry_t *fdc_geometry_from_cmos(uint8_t type);
void fdc_parse_cmos_drive_types(uint8_t reg10, uint8_t types[2]);
int fdc_lba_to_chs(const fdc_geometry_t *geom, uint32_t lba, fdc_chs_t *chs);
uint32_t fdc_chs_to_lba(const fdc_geometry_t *geom, uint16_t cylinder,
                        uint8_t head, uint8_t sector);
int fdc_dma_window_valid(uintptr_t phys_addr, size_t len);

void floppy_init(void);

#endif
