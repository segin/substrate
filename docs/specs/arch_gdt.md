# GDT/TSS Specification (i386)

## Overview
The Global Descriptor Table (GDT) and Task State Segment (TSS) are fundamental x86 structures required for memory protection, segment management, and privilege transitions (interrupt stacks).

## Design
- **GDT Layout:**
    - Entry 0: Null
    - Entry 1: Kernel Code (0x08) - DPL 0
    - Entry 2: Kernel Data (0x10) - DPL 0
    - Entry 3: User Code (0x1B) - DPL 3
    - Entry 4: User Data (0x23) - DPL 3
    - Entry 5: TSS (0x28)
- **TSS:** Used to store the kernel stack pointer (`esp0`) for use during transitions from user mode to kernel mode.

## API
### `void gdt_init(void)`
Initializes the GDT entries and loads the TSS.

### `void set_kernel_stack(uintptr_t stack)`
Updates the `esp0` field in the TSS to point to the current thread's kernel stack.

## Constraints
- i386 specific. x86_64 uses a different GDT/TSS model (no hardware task switching).
