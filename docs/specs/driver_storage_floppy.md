# Floppy Driver Specification

## Overview
- Driver location: `sys/drivers/storage/floppy/`
- Hardware target: ISA NEC uPD765 / Intel 82077AA compatible floppy disk controller
- Device nodes: `/dev/storage/floppy0` through `/dev/storage/floppy3` for currently detected drives

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
- Secondary-controller drives are enumerated as `floppy2` and `floppy3`; because legacy CMOS does not describe them, the driver currently falls back to the common 1.44MB geometry until richer media detection lands
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
- Track formatting is exposed through `FLOPPY_IOCTL_FORMAT_TRACK` in [include/sys/floppy.h](/home/segin/test/include/sys/floppy.h), using a DMA-fed `(C,H,R,N)` tuple table for each sector header
- Motors spin up on demand and are shut down from the periodic timer path after roughly 2.5s of inactivity
- Media-change handling checks `DIR.DSKCHG`, invalidates cached geometry/current-cylinder state, restores the drive baseline geometry, and clears the latch by seeking to cylinder 1 and back to cylinder 0
- Error handling decodes `ST0`, `ST1`, and `ST2`
- Recoverable failures trigger recalibrate + retry up to three attempts

## Current Limits
- Format-track, disk-change, and full four-drive support in the runtime driver
  are still maturing (see the driver source and the `BUGS` section of
  `floppy(4)`).

## Bootable Floppy Image Layout

The bootable floppy image is built by `sys/arch/i386/floppy/mkfloppy.py`
from a kernel `zimage` (a bzImage-style setup header followed by the kernel
body).  Every floppy is a standard 1.44M image: `2880` sectors of `512`
bytes (`1474560` bytes total; `FLOPPY_SPT = 18`, `FLOPPY_HEADS = 2`).

Disk 1 always begins with the two-stage loader:

- sector `0`: `stage1.bin` (the 512-byte boot sector; loads stage2)
- sectors `1 .. S`: `stage2.bin` (the real-mode kernel loader, `S` sectors)
- sectors `(1 + S) ..`: the kernel `zimage`, beginning at
  `kernel_lba = 1 + S`.  The first `KERNEL_SETUP_SECTORS` sectors are the
  setup image; the remainder is the kernel body loaded to `0x00100000`.

### Single-disk image (kernel fits on one floppy)

When `kernel_lba + kernel_sectors <= 2880` the whole kernel fits on one
floppy and the image is the classic `stage1 | stage2 | zimage` layout in a
single file.  This is the historical layout and is produced byte-for-byte
unchanged.

### Multi-disk image (kernel spans several floppies)

When the kernel does not fit, `mkfloppy.py` splits it across `N` floppies:

- **Disk 1** (`<output>`): `stage1 | stage2 |` the leading
  `2880 - kernel_lba` kernel sectors, filling the disk to its end.  The
  setup image always fits here (it is at the front of the kernel).
- **Disks 2..N** (`<output>.2`, `<output>.3`, ...): the raw kernel
  continuation, `2880` sectors per disk, starting at disk-relative LBA `0`.
  The final disk is zero-padded to a full `1474560`-byte image.

`mkfloppy.py` prints an `N`-disk layout summary and passes the loader
`FLOPPY_DISK_SECTORS` (2880), `KERNEL_LBA`, `KERNEL_SECTORS` (the total
kernel-sector count across all disks) and `KERNEL_SETUP_SECTORS` via
`nasm -D`.

### Loader behavior and disk swaps

`stage2.asm` loads the setup image from disk 1, then streams the kernel body
to `0x00100000` a chunk at a time.  Each read is capped both at the current
track boundary and at the current disk boundary (`FLOPPY_DISK_SECTORS`), so a
single BIOS read never crosses either.  On-screen it prints `Loading kernel`
followed by one progress dot per chunk copied.

When the current disk's LBA reaches `FLOPPY_DISK_SECTORS` and kernel body
sectors still remain, the loader prompts:

```
Please insert boot disk N and press any key to continue...
```

waits for a keypress (`int 0x16`), resets the drive (`int 0x13`, `AH=0`) so
the controller re-reads the freshly inserted media, rewinds the current-disk
LBA to `0`, and resumes.  A single-disk kernel never reaches the disk
boundary and therefore never prompts.  Progress is also emitted to the
`0xE9` debug port as the byte stream
`A P S L (k r g p q m u c)* [W D ...] J`, where `W`/`D` bracket each disk
swap and `J` marks the jump to the kernel setup entry.
