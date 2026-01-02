# Local APIC (LAPIC) Specification

## Overview
The Local APIC (Advanced Programmable Interrupt Controller) manages interrupts for a specific CPU core. It provides high-resolution timers, inter-processor interrupts (IPIs), and handles local interrupt sources.

## Design
- **Base Address:** Located via ACPI MADT (default `0xFEE00000`).
- **Registers:** Memory-mapped I/O (MMIO).
- **Initialization Sequence:**
    1. Map the LAPIC physical address into the kernel address space.
    2. Set the Spurious Interrupt Vector (SVR) to enable the APIC.
    3. Configure LVT (Local Vector Table) entries (Timer, LINT0, LINT1, Error).
    4. Initialize the LAPIC Timer (divide configuration, initial count).
- **Spurious Interrupts:** Designated interrupt vector (usually `0xFF`) to handle spurious IRQs.

## API
### `void lapic_init(void)`
Initializes the LAPIC for the current core.

### `void lapic_send_eoi(void)`
Sends an "End of Interrupt" signal to the LAPIC.

### `uint32_t lapic_get_id(void)`
Returns the unique APIC ID of the current core.

## Constraints
- Requires functional `pmap_enter` for MMIO mapping.
- Architecture-specific (i386 and x86_64).
