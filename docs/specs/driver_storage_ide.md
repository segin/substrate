# ATA/IDE Driver

## Scope

The ATA/IDE driver provides legacy PATA transport for:
- ISA compatibility-mode controllers
- PCI IDE controllers using compatibility or native BAR layouts
- ATA disks and ATAPI packet devices

Device nodes are published under `/dev/storage/ideN`.

## Channel Model

The driver tracks up to four channels:

- primary: I/O `0x1F0`, control `0x3F6`, IRQ `14`
- secondary: I/O `0x170`, control `0x376`, IRQ `15`
- tertiary: I/O `0x1E8`, control `0x3EE`, IRQ `11`
- quaternary: I/O `0x168`, control `0x36E`, IRQ `10`

Primary and secondary may be overridden by PCI native-mode BARs. Tertiary and
quaternary remain legacy fixed-base channels.

## Discovery

Discovery starts from default channel definitions, then applies PCI IDE native
mode overrides for primary and secondary channels when a class `01:01`
controller is present.

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
- bounded BSY/DRQ waits
- 400ns command delays through alternate-status reads

Bus-master DMA support provides:
- PRDT setup with up to 32 entries
- 64KB boundary splitting
- IRQ-driven completion

ATAPI transport supports PACKET commands over the same channel model and backs
the SCSI mid-layer helper path.
