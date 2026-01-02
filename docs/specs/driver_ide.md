# ATA/IDE Driver Specification

## Overview
The ATA/IDE driver provides support for legacy hard disk drives using Programmed I/O (PIO) mode. It supports both Primary and Secondary channels.

## Implementation
- **Channels:**
    - Primary: I/O base 0x1F0, Control base 0x3F6.
    - Secondary: I/O base 0x170, Control base 0x376.
- **Commands:**
    - `ATA_CMD_IDENTIFY` (0xEC): Retrieve drive information.
    - `ATA_CMD_READ_PIO` (0x20): Read sectors using PIO (28-bit LBA).
    - `ATA_CMD_READ_PIO_EXT` (0x24): Read sectors using PIO (48-bit LBA).
    - `ATA_CMD_READ_DMA_EXT` (0x25): Read sectors using DMA (48-bit LBA).
- **Status Checking:** Polls the Status Register for `BSY` (Busy) and `DRQ` (Data Request) bits.

## API
### `int ide_read_sectors(uint16_t bus, uint8_t drive, uint32_t lba, uint8_t count, void *buffer)`
Reads a specified number of sectors using 28-bit LBA.

### `int ide_read_sectors_ext(uint16_t bus, uint8_t drive, uint64_t lba, uint16_t count, void *buffer)`
Reads sectors using 48-bit LBA support.

### `int ide_dma_setup(uint16_t bus, uint8_t drive, uint64_t lba, uint16_t count, void *phys_addr, int write)`
Configures the Bus Master IDE controller for a UDMA transfer.

## Constraints
- DMA implementation is currently a skeleton (PRDT setup needed).
- No interrupt support yet (uses polling).
