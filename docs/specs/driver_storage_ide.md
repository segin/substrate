# ATA/IDE Driver

## Scope

The ATA/IDE driver provides legacy PATA transport for:
- ISA compatibility-mode controllers
- PCI IDE controllers using compatibility or native BAR layouts
- ATA disks and ATAPI packet devices

Device nodes are published under `/dev/storage/ideN`.
The slot mapping is fixed by channel and drive:

- `ide0` primary master
- `ide1` primary slave
- `ide2` secondary master
- `ide3` secondary slave
- `ide4` tertiary master
- `ide5` tertiary slave
- `ide6` quaternary master
- `ide7` quaternary slave

## Channel Model

The driver tracks up to four channels:

- primary: I/O `0x1F0`, control `0x3F6`, IRQ `14`
- secondary: I/O `0x170`, control `0x376`, IRQ `15`
- tertiary: I/O `0x1E8`, control `0x3EE`, IRQ `11`
- quaternary: I/O `0x168`, control `0x36E`, IRQ `10`

Primary and secondary may be overridden by PCI native-mode BARs on the first
PCI IDE controller function. Additional PCI IDE functions are consumed as
additional channel pairs and populate the tertiary/quaternary slots from their
BAR layout when present.
When a PCI IDE controller exposes a valid interrupt line, the corresponding
channel IRQ is taken from PCI configuration space and registered through the
generic IRQ layer. Native-mode PCI channels request their IRQs as shared lines.

## Discovery

Discovery starts from default channel definitions, then walks every PCI class
`01:01` IDE controller function in enumeration order. The first function owns
the primary/secondary pair, and later functions claim the tertiary/quaternary
pair when capacity remains.

On ISA-only systems the driver consumes ISA legacy-bus hints for:
- `ide-primary`
- `ide-secondary`
- `ide-tertiary`
- `ide-quaternary`

If any IDE ISA hints exist, only hinted channels are probed. Otherwise all
four default channels are probed directly and a floating bus is rejected when
status reads back `0xFF`.

## IDENTIFY Parsing

`ide_parse_identify_data()` normalizes IDENTIFY / IDENTIFY PACKET data into
`ide_device_t`:

- serial: words `10..19`
- firmware revision: words `23..26`
- model: words `27..46`
- LBA28 size: words `60..61`
- LBA48 size: words `100..103`
- command-set flags: words `82..83`
- DMA capability/mode bits: words `49`, `63`, `88`
- NCQ capability: word `76` bit `8`
- TRIM capability: word `169` bit `0`

Strings are byte-swapped from ATA word order and right-trimmed.

## Transfer Model

PIO transfers support:
- LBA28 read/write
- LBA48 read/write
- fixed bus-to-channel mapping for all four legacy ISA-era channel bases
- bounded BSY/DRQ waits
- timeout classes tuned for real spinning media:
  - generic ready waits: 5s
  - PIO data phase waits: 10s
  - IDENTIFY / IDENTIFY PACKET: 30s
  - ATAPI PACKET command/data phases: 30s
- 400ns command delays through alternate-status reads
- ATA error-bit decoding in diagnostic paths
- block-read retries before surfacing failure
- offline marking after repeated transfer failure

Bus-master DMA support provides:
- PRDT setup with up to 32 entries
- 64KB boundary splitting
- IRQ-driven completion with a bounded 30s timeout
- DMA transfer-mode programming via `SET FEATURES` subcommand `0x03`
  using the highest supported UDMA mode, or MWDMA as fallback

ATAPI transport supports PACKET commands over the same channel model and backs
the SCSI mid-layer helper path.

## Recovery

The driver supports ATA software reset on a per-channel basis:

- assert `SRST` in the device-control register
- deassert `SRST` after the reset pulse
- wait for BSY to clear on both master and slave positions
- reissue IDENTIFY / IDENTIFY PACKET on the channel to refresh in-memory
  device metadata

Repeated transfer failures attempt a channel reset once before the driver marks
the failing device offline.
