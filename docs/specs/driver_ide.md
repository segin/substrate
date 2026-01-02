# ATA/IDE Driver Specification

## Overview
The ATA/IDE driver provides support for legacy hard disk drives using Programmed I/O (PIO) mode. It supports both Primary and Secondary channels.

## Implementation
- **Channels:**
    - Primary: I/O base 0x1F0, Control base 0x3F6.
    - Secondary: I/O base 0x170, Control base 0x376.
- **Commands:**
    - `ATA_CMD_IDENTIFY` (0xEC): Retrieve drive information.
    - `ATA_CMD_READ_PIO` (0x20): Read sectors using PIO.
    - `ATA_CMD_WRITE_PIO` (0x30): Write sectors using PIO.
- **Status Checking:** Polls the Status Register for `BSY` (Busy) and `DRQ` (Data Request) bits.

## API
### `int ide_read_sectors(uint16_t bus, uint8_t drive, uint32_t lba, uint8_t count, void *buffer)`
Reads a specified number of sectors starting from an LBA.

### `int ide_write_sectors(uint16_t bus, uint8_t drive, uint32_t lba, uint8_t count, const void *buffer)`
Writes a specified number of sectors.

### `int ide_identify(uint16_t bus, uint8_t drive, void *buffer)`
Identifies the drive and fills the buffer with 512 bytes of identification data.

## Constraints
- Current implementation only supports 28-bit LBA.
- No DMA support yet.
- No interrupt support yet (uses polling).
