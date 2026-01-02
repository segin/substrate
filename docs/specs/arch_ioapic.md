# IO-APIC Specification

## Overview
The I/O APIC (Advanced Programmable Interrupt Controller) is responsible for routing external hardware interrupts (IRQs) to specific Local APICs (CPU cores). It replaces the legacy 8259 PIC.

## Design
- **Base Address:** Located via ACPI MADT (usually `0xFEC00000`).
- **Registers:** Accessed via two MMIO registers: `IOREGSEL` (Index) and `IOWIN` (Data).
- **Redirection Table (REDTBL):** A table of 64-bit entries (one per IRQ). Each entry contains:
    - Interrupt Vector.
    - Delivery Mode (Fixed, Lowest Priority, etc.).
    - Destination Mode (Physical/Logical).
    - Mask bit.
    - Destination Local APIC ID.
- **Initialization:**
    1. Locate IO-APIC MMIO base.
    2. Identify IRQ to GSI (Global System Interrupt) mapping from MADT.
    3. Disable legacy PIC by masking all interrupts.
    4. Provide functions to route specific IRQs to cores.

## API
### `void ioapic_init(uintptr_t base)`
Initializes the IO-APIC at the given address.

### `void ioapic_set_routing(uint8_t irq, uint8_t vector, uint32_t cpu_id)`
Routes a hardware IRQ to a specific CPU's Local APIC.

### `void ioapic_set_mask(uint8_t irq, bool mask)`
Enable or disable a specific interrupt source.

## Constraints
- Requires functional `pmap_enter` for MMIO mapping.
- Shared logic between i386 and x86_64.
