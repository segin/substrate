# Floppy Driver Specification

## Overview
- Driver location: `sys/drivers/storage/floppy/`
- Hardware target: ISA NEC uPD765 / Intel 82077AA compatible floppy disk controller
- Device nodes: `/dev/storage/fd0` through `/dev/storage/fd3` for currently detected drives

## Controller Model
- Controllers are probed at legacy ISA bases:
  - primary: `0x3F0`, IRQ `6`
  - secondary: `0x370`, IRQ `10`
- Presence detection uses the main status register at `base + 0x04`.
- Reset is issued by toggling `DOR.RESET` through `base + 0x02`, waiting for the controller IRQ, and draining `SENSE INTERRUPT STATUS` once per drive slot.
- The current implementation configures the controller with:
  - `CONFIGURE`
  - `SPECIFY`
  - data-rate programming through `CCR`

## Register Map
- `base + 0x02`: Digital Output Register (`DOR`)
- `base + 0x04`: Main Status Register (`MSR`)
- `base + 0x05`: FIFO / Data Register
- `base + 0x07` read: Digital Input Register (`DIR`)
- `base + 0x07` write: Configuration Control Register (`CCR`)

## DMA Programming
- Uses ISA DMA channel `2`
- DMA window must remain below `16MB`
- DMA buffer must not cross a `64KB` boundary
- Driver allocates a low-memory bounce page and programs:
  - address ports `0x04`, `0x05`
  - page port `0x81`
  - mask/mode/flip-flop ports `0x0A`, `0x0B`, `0x0C`

## Drive Detection
- CMOS register `0x10` provides drive type hints for drives `0` and `1`
- Secondary-controller drives are enumerated as `fd2` and `fd3`; because legacy CMOS does not describe them, the driver currently falls back to the common 1.44MB geometry until richer media detection lands
- Supported geometries:
  - `360K`
  - `720K`
  - `1.2M`
  - `1.44M`
  - `2.88M`

## I/O Path
- Sector interface is exposed through the block-device layer
- Driver converts `LBA <-> CHS` from the selected geometry
- Read and write operations are DMA-backed and batched in up-to-4KB commands; when a command spans head 0 to head 1 on the same cylinder the driver sets the controller MT bit
- Motors spin up on demand and are shut down from the periodic timer path after roughly 2.5s of inactivity
- Error handling decodes `ST0`, `ST1`, and `ST2`
- Recoverable failures trigger recalibrate + retry up to three attempts

## Current Limits
- Disk-change handling is not yet implemented
- Format-track support is not yet implemented
- Multi-track command batching is not yet implemented
