#ifndef _IDE_H
#define _IDE_H

#include <stdint.h>
#include <stddef.h>

/*
 * ATA/IDE Driver Header
 *
 * Supports:
 * - PIO Mode transfers (LBA28 and LBA48)
 * - DMA transfers (Bus Master IDE)
 * - ATAPI packet commands
 * - Up to four channels with Master/Slave drives
 */

/*
 * ============================================================
 * Standard I/O Ports
 * ============================================================
 */
#define ATA_PRIMARY_IO      0x1F0   /* Primary channel I/O base */
#define ATA_SECONDARY_IO    0x170   /* Secondary channel I/O base */
#define ATA_TERTIARY_IO     0x1E8   /* Tertiary channel I/O base */
#define ATA_QUATERNARY_IO   0x168   /* Quaternary channel I/O base */
#define ATA_PRIMARY_CTRL    0x3F6   /* Primary channel control base */
#define ATA_SECONDARY_CTRL  0x376   /* Secondary channel control base */
#define ATA_TERTIARY_CTRL   0x3EE   /* Tertiary channel control base */
#define ATA_QUATERNARY_CTRL 0x36E   /* Quaternary channel control base */

#define ATA_PRIMARY_IRQ      14
#define ATA_SECONDARY_IRQ    15
#define ATA_TERTIARY_IRQ     11
#define ATA_QUATERNARY_IRQ   10

#define MAX_IDE_CHANNELS 4
#define MAX_IDE_DEVICES  (MAX_IDE_CHANNELS * 2)
#define IDE_DEVICE_INDEX(channel, drive) (((channel) * 2) + ((drive) & 1))

/*
 * ============================================================
 * Standard ATA Registers (offset from I/O base)
 * ============================================================
 */
#define ATA_REG_DATA       0x00  /* Data Register (R/W) */
#define ATA_REG_ERROR      0x01  /* Error Register (R) */
#define ATA_REG_FEATURES   0x01  /* Features Register (W) */
#define ATA_REG_SEC_COUNT  0x02  /* Sector Count */
#define ATA_REG_LBA_LOW    0x03  /* LBA Low (7:0) */
#define ATA_REG_LBA_MID    0x04  /* LBA Mid (15:8) */
#define ATA_REG_LBA_HIGH   0x05  /* LBA High (23:16) */
#define ATA_REG_DEVICE     0x06  /* Device/Head Register */
#define ATA_REG_STATUS     0x07  /* Status Register (R) */
#define ATA_REG_COMMAND    0x07  /* Command Register (W) */

/* Control Register (offset from Control base, usually base+0x206) */
#define ATA_REG_CONTROL    0x00  /* Device Control Register */
#define ATA_REG_ALTSTATUS  0x00  /* Alternate Status Register */

/*
 * ============================================================
 * ATA Commands
 * ============================================================
 */
/* PIO Commands */
#define ATA_CMD_READ_PIO       0x20  /* Read Sectors (LBA28) */
#define ATA_CMD_WRITE_PIO      0x30  /* Write Sectors (LBA28) */
#define ATA_CMD_READ_PIO_EXT   0x24  /* Read Sectors Ext (LBA48) */
#define ATA_CMD_WRITE_PIO_EXT  0x34  /* Write Sectors Ext (LBA48) */

/* DMA Commands */
#define ATA_CMD_READ_DMA       0xC8  /* Read DMA (LBA28) */
#define ATA_CMD_WRITE_DMA      0xCA  /* Write DMA (LBA28) */
#define ATA_CMD_READ_DMA_EXT   0x25  /* Read DMA Ext (LBA48) */
#define ATA_CMD_WRITE_DMA_EXT  0x35  /* Write DMA Ext (LBA48) */

/* Identification */
#define ATA_CMD_IDENTIFY       0xEC  /* Identify Device */
#define ATA_CMD_IDENTIFY_ATAPI 0xA1  /* Identify Packet Device */

/* ATAPI */
#define ATA_CMD_PACKET         0xA0  /* ATAPI Packet Command */

/* Cache/Flush */
#define ATA_CMD_CACHE_FLUSH    0xE7  /* Flush Cache */
#define ATA_CMD_CACHE_FLUSH_EXT 0xEA /* Flush Cache Ext (LBA48) */

/*
 * ============================================================
 * Status Register Bits
 * ============================================================
 */
#define ATA_SR_BSY         0x80  /* Busy */
#define ATA_SR_DRDY        0x40  /* Device Ready */
#define ATA_SR_DF          0x20  /* Device Fault */
#define ATA_SR_DSC         0x10  /* Seek Complete */
#define ATA_SR_DRQ         0x08  /* Data Request */
#define ATA_SR_CORR        0x04  /* Corrected Data */
#define ATA_SR_IDX         0x02  /* Index */
#define ATA_SR_ERR         0x01  /* Error */

/*
 * ============================================================
 * Error Register Bits
 * ============================================================
 */
#define ATA_ER_BBK         0x80  /* Bad Block */
#define ATA_ER_UNC         0x40  /* Uncorrectable Data */
#define ATA_ER_MC          0x20  /* Media Changed */
#define ATA_ER_IDNF        0x10  /* ID Not Found */
#define ATA_ER_MCR         0x08  /* Media Change Request */
#define ATA_ER_ABRT        0x04  /* Aborted Command */
#define ATA_ER_TK0NF       0x02  /* Track 0 Not Found */
#define ATA_ER_AMNF        0x01  /* Address Mark Not Found */

/*
 * ============================================================
 * Device Control Register Bits
 * ============================================================
 */
#define ATA_CTRL_SRST      0x04  /* Software Reset */
#define ATA_CTRL_NIEN      0x02  /* Interrupt Enable (inverted) */
#define ATA_CTRL_HOB       0x80  /* High Order Byte (LBA48) */

/*
 * ============================================================
 * Bus Master IDE Registers (offset from BM base)
 * ============================================================
 */
#define BM_REG_COMMAND     0x00  /* Bus Master Command Register */
#define BM_REG_STATUS      0x02  /* Bus Master Status Register */
#define BM_REG_PRDT        0x04  /* PRDT Address (32-bit) */

/* Bus Master Command Register bits */
#define BM_CMD_START       0x01  /* Start/Stop DMA */
#define BM_CMD_WRITE       0x08  /* Direction: 0=read, 1=write */

/* Bus Master Status Register bits */
#define BM_STAT_ACTIVE     0x01  /* DMA Active */
#define BM_STAT_ERROR      0x02  /* DMA Error */
#define BM_STAT_INTERRUPT  0x04  /* Interrupt (write 1 to clear) */
#define BM_STAT_DRIVE0_DMA 0x20  /* Drive 0 DMA capable */
#define BM_STAT_DRIVE1_DMA 0x40  /* Drive 1 DMA capable */
#define BM_STAT_SIMPLEX    0x80  /* Simplex mode only */

/*
 * ============================================================
 * Physical Region Descriptor Table (PRDT) Entry
 * ============================================================
 *
 * Each entry describes a contiguous physical memory region.
 * Must be aligned to 4-byte boundary.
 * Array must not cross a 64KB boundary.
 */
typedef struct __attribute__((packed)) {
    uint32_t phys_addr;   /* Physical base address (must be word-aligned) */
    uint16_t byte_count;  /* Byte count (0 = 64KB) */
    uint16_t reserved : 15;
    uint16_t eot : 1;     /* End Of Table flag (1 = last entry) */
} prdt_entry_t;

/* Maximum PRD entries (enough for 256 sectors = 128KB) */
#define MAX_PRD_ENTRIES 32

/*
 * ============================================================
 * IDE Channel State
 * ============================================================
 */
typedef struct {
    uint16_t io_base;      /* I/O Base (0x1F0 or 0x170) */
    uint16_t ctrl_base;    /* Control Base (0x3F6 or 0x376) */
    uint16_t bm_base;      /* Bus Master Base (from PCI BAR4) */
    uint8_t  irq;          /* IRQ number (14 or 15) */
    uint8_t  no_intr;      /* Interrupt disable flag */
    uint8_t  dma_capable;  /* Bus Master DMA detected */
    
    /* PRDT for this channel (aligned to 4 bytes, 64KB boundary safe) */
    prdt_entry_t prdt[MAX_PRD_ENTRIES] __attribute__((aligned(4)));
} ide_channel_t;

/*
 * ============================================================
 * IDE Device State
 * ============================================================
 */
typedef struct {
    uint8_t  present;      /* Device present */
    uint8_t  type;         /* 0=ATA, 1=ATAPI */
    uint8_t  channel;      /* 0=Primary, 1=Secondary, 2=Tertiary, 3=Quaternary */
    uint8_t  drive;        /* 0=Master, 1=Slave */
    uint16_t signature;    /* Drive signature */
    uint16_t capabilities; /* Capabilities from IDENTIFY */
    uint32_t command_sets; /* Supported command sets */
    uint64_t size;         /* Size in sectors */
    char     serial[21];   /* Serial string */
    char     firmware[9];  /* Firmware revision string */
    char     model[41];    /* Model string */
    uint32_t feature_flags;/* Parsed feature bits */
    uint8_t  mwdma_modes;  /* Supported multiword DMA modes */
    uint8_t  udma_modes;   /* Supported ultra DMA modes */
    uint8_t  dma_mode;     /* DMA mode (0=none, 1=UDMA, 2=MWDMA) */
    uint8_t  offline;      /* Driver marked device offline after repeated errors */
} ide_device_t;

#define IDE_FEATURE_DMA    0x00000001U
#define IDE_FEATURE_LBA48  0x00000002U
#define IDE_FEATURE_SMART  0x00000004U
#define IDE_FEATURE_NCQ    0x00000008U
#define IDE_FEATURE_TRIM   0x00000010U

#define IDE_TIMEOUT_READY_MS     5000
#define IDE_TIMEOUT_DATA_MS      10000
#define IDE_TIMEOUT_IDENTIFY_MS  30000
#define IDE_TIMEOUT_PACKET_MS    30000
#define IDE_TIMEOUT_DMA_MS       30000

/*
 * ============================================================
 * Public API
 * ============================================================
 */

/* Initialization */
void ide_init(void);
void ide_dma_init(uint16_t bm_base_primary, uint16_t bm_base_secondary);

/* PIO Transfers */
int ide_read_sectors(uint16_t bus, uint8_t drive, uint32_t lba, 
                     uint8_t count, void *buffer);
int ide_read_sectors_ext(uint16_t bus, uint8_t drive, uint64_t lba, 
                         uint16_t count, void *buffer);
int ide_write_sectors(uint16_t bus, uint8_t drive, uint32_t lba, 
                      uint8_t count, const void *buffer);
int ide_write_sectors_ext(uint16_t bus, uint8_t drive, uint64_t lba, 
                          uint16_t count, const void *buffer);

/* DMA Transfers */
int ide_dma_read(uint8_t channel, uint8_t drive, uint64_t lba, 
                 uint16_t count, void *buffer);
int ide_dma_write(uint8_t channel, uint8_t drive, uint64_t lba, 
                  uint16_t count, const void *buffer);
int ide_dma_setup(uint16_t bus, uint8_t drive, uint64_t lba, 
                  uint16_t count, void *phys_addr, int write);
int ide_prdt_build_entries(prdt_entry_t *prdt, size_t max_entries,
                           uint32_t phys_addr, uint32_t byte_count);

/* Identification */
int ide_identify(uint16_t bus, uint8_t drive, void *buffer);
int ide_identify_atapi(uint16_t bus, uint8_t drive, void *buffer);
void ide_parse_identify_data(ide_device_t *dev, const uint16_t *buffer,
                             uint8_t type, uint8_t channel, uint8_t drive);
size_t ide_decode_error(uint8_t error, char *buf, size_t size);

/* ATAPI Packet Commands */
int ide_atapi_packet(uint8_t channel, uint8_t drive, 
                     const uint8_t *packet, uint8_t packet_len,
                     void *buffer, uint32_t buffer_len, int write);
int ide_atapi_read_capacity(uint8_t channel, uint8_t drive, 
                            uint32_t *lba, uint32_t *block_size);
int ide_atapi_read_sectors(uint8_t channel, uint8_t drive, 
                           uint32_t lba, uint16_t count, void *buffer);
int ide_atapi_read_toc(uint8_t channel, uint8_t drive,
                       uint8_t start_track, void *buffer, uint16_t buffer_len);

/* Bus Master Control */
void ide_bm_start(uint8_t channel, int write);
void ide_bm_stop(uint8_t channel);
uint8_t ide_bm_status(uint8_t channel);
void ide_bm_clear_interrupt(uint8_t channel);
int ide_prdt_setup(uint8_t channel, void *buffer, uint32_t byte_count);

/* IRQ Handler */
void ide_irq_handler(int irq);

/* Low-Level Register Access */
void ide_write_reg(uint8_t channel, uint8_t reg, uint8_t data);
uint8_t ide_read_reg(uint8_t channel, uint8_t reg);
void ide_write_ctrl(uint8_t channel, uint8_t data);
uint8_t ide_read_ctrl(uint8_t channel);

#endif /* _IDE_H */
