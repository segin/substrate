# Local APIC (LAPIC) Specification

## Overview
The Local APIC (Advanced Programmable Interrupt Controller) manages interrupts for a specific CPU core. It provides high-resolution timers, inter-processor interrupts (IPIs), and handles local interrupt sources.

## Design
- **Base Address:** Located via ACPI MADT (default `0xFEE00000`).
- **Registers:** Memory-mapped I/O (MMIO).
- **i386 Mapping Model:** The bootstrap page tables provide LAPIC MMIO reachability at the default base, and `lapic_set_base()` updates the physical base selected by firmware tables.
- **Initialization Sequence:**
    1. Establish or reuse the LAPIC physical MMIO base in the kernel address space.
    2. Set the Spurious Interrupt Vector (SVR) to enable the APIC.
    3. Configure LVT (Local Vector Table) entries (Timer, LINT0, LINT1, Error).
    4. Initialize the LAPIC Timer (divide configuration, initial count).
- **Supported IPI Modes:** Fixed, Lowest Priority, NMI, INIT, and SIPI delivery modes are emitted through the ICR path.
- **Spurious Interrupts:** Designated interrupt vector (usually `0xFF`) to handle spurious IRQs.

## API
### `void lapic_init(void)`
Initializes the LAPIC for the current core.

### `void lapic_send_eoi(void)`
Sends an "End of Interrupt" signal to the LAPIC.

### `uint32_t lapic_get_id(void)`
Returns the unique APIC ID of the current core.

## Constraints
- i386 currently relies on bootstrap mapping for the default LAPIC window rather than a late dynamic `pmap_enter()` remap.
- Architecture-specific (i386 and x86_64).
