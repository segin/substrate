# Exception Handling Specification (i386)

## Overview
Exception handling provides the kernel with a mechanism to catch and respond to CPU-generated error conditions (e.g., Division by Zero, Page Faults, General Protection Faults).

## Design
- **Interrupt Descriptor Table (IDT):** Gates 0-31 are reserved for hardware exceptions.
- **ISRs:** Assembly stubs (`isr.S`) capture the exception number and error code (if any), then transition to the C handler.
- **C Dispatcher (`isr_handler`):** Identifies the exception type and either attempts recovery (e.g., via `vm_fault`) or triggers a kernel panic.

## API
### `void idt_init(void)`
Configures the IDT entries for all 32 exceptions and loads the IDT pointer.

### `void isr_handler(registers_t *regs)`
The high-level dispatcher for all exceptions.

## Constraints
- i386 specific. x86_64 uses a different stack layout for exceptions.
- Currently, most unhandled exceptions result in a kernel panic.
