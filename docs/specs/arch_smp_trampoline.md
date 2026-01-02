# SMP Trampoline Specification

## Overview
The SMP Trampoline is a small piece of 16-bit real mode code that is executed by Application Processors (APs) when they are first started by the Bootstrap Processor (BSP) via an IPI (Inter-Processor Interrupt). Its job is to transition the AP from real mode to 32-bit protected mode (or 64-bit long mode) and jump into the kernel's C entry point.

## Design
- **Location:** Must be placed in the first 1MB of physical memory (usually `0x1000` to `0x9000`) to be accessible in real mode.
- **Initialization Sequence (16-bit):**
    1. Load a temporary Global Descriptor Table (GDT).
    2. Set the PE (Protection Enable) bit in CR0.
    3. Perform a far jump to a 32-bit code segment.
- **Initialization Sequence (32-bit):**
    1. Set up data segments (DS, ES, SS).
    2. Load the kernel stack pointer for this CPU.
    3. Call the kernel's AP entry point (`smp_ap_entry`).
- **Communication:** The BSP provides the target stack pointer and entry point address at a fixed offset relative to the trampoline code.

## API (Internal)
### `void smp_load_trampoline(void)`
Copies the trampoline code to its designated physical address.

### `void smp_boot_ap(uint8_t apic_id)`
Sends the INIT and STARTUP IPIs to a specific AP to trigger the trampoline.

## Constraints
- i386/x86_64 specific.
- Requires 4KB aligned physical page in the low 1MB.
