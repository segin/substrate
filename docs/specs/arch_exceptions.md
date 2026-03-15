# Exception Handling Specification (i386)

## Overview
Exception handling provides the kernel with a mechanism to catch and respond to CPU-generated error conditions (e.g., Division by Zero, Page Faults, General Protection Faults).

## Design
- **Interrupt Descriptor Table (IDT):** Gates 0-31 are reserved for hardware exceptions.
- **ISRs:** Assembly stubs (`isr.S`) capture the exception number and error code (if any), then transition to the C handler.
- **C Dispatcher (`isr_handler`):** Identifies the exception type and either attempts recovery (e.g., via `vm_fault`) or triggers a kernel panic.
- **Diagnostics:** Before panicking, the i386 dispatcher emits the exception name plus saved register state (`EIP`, `CS`, `ERR`, `EAX`..`EDX`, `ESI`, `EDI`, `EBP`, `ESP`). Invalid-opcode faults additionally dump up to 16 instruction bytes at `EIP` when the address is safe to read.
- **VM86 Escape Hatch:** A general-protection fault with `EFLAGS.VM` set is diverted to the VM86 handler instead of the normal protected-mode exception path.

## API
### `void idt_init(void)`
Configures the IDT entries for all 32 exceptions and loads the IDT pointer.

### `void isr_handler(registers_t *regs)`
The high-level dispatcher for all exceptions.

## Constraints
- i386 specific. x86_64 uses a different stack layout for exceptions.
- Currently, most unhandled exceptions result in a kernel panic.

## Verification Status
- `host_test_idt_diag` validates:
  - VM86 GPF dispatch from the IDT exception path
  - invalid-opcode diagnostics including register and byte dumps
- `host_test_panic_diag` validates the panic banner, message, stack-trace callout, and halt footer.
