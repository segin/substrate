# AHCI Driver Specification

## Overview
The AHCI driver provides SATA and SATAPI support for controllers implementing the AHCI 1.0 specification.

## Discovery and Registration
- Registers a PCI driver against class `0x01/0x06/0x01` (Mass Storage / SATA / AHCI 1.0) through the device-model framework.
- Maps BAR5 via `pci_iomap()`.
- Enables bus mastering.
- Takes AHCI ownership from BIOS.

## Memory Management
- Allocates DMA-coherent command lists, FIS receive areas, and command tables per port.

## Port Management
- Probes each implemented port for device signatures.
- Issues `IDENTIFY DEVICE` for SATA disks.
- Publishes detected SATA disks as `/dev/storage/sataN` block devices.
- Wraps SATAPI (ATAPI-over-SATA) optical drives behind the SCSI mid-layer via `scsi_link_t`.

## Transfer Model
- The driver operates in polling mode.
- Uses a single command slot per port in the current implementation.
