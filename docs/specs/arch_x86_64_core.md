# x86_64 Core Architecture

## Scope

This document covers the currently implemented core x86_64 architectural
surfaces in Substrate outside the PMAP-specific design:
- long-mode bootstrap assembly artifacts
- long-mode GDT/TSS layout
- x86_64 IDT gate layout
- x86_64 ISR and context-switch assembly artifacts
- syscall MSR programming and dispatch contract

It does not claim full runtime bring-up or scheduler completeness for the
x86_64 port.

## GDT/TSS Contract

The x86_64 GDT establishes the canonical selector layout:
- `0x08` kernel code
- `0x10` kernel data
- `0x18` user data
- `0x20` user code
- `0x28` TSS descriptor

The user-data selector precedes user-code so the layout is compatible with the
`SYSRET` selector derivation rules.

Per-CPU state:
- one GDT per CPU
- one `tss64` per CPU
- one IST stack each for NMI, double fault, and machine check per CPU

The current TSS contract uses:
- `rsp0` for privilege transitions into the kernel
- `ist1` for NMI
- `ist2` for double fault
- `ist3` for machine check
- `iopb_offset = sizeof(struct tss64)` (no I/O bitmap exposed yet)

## Bootstrap and Assembly Entry Artifacts

The x86_64 bootstrap path currently provides:
- `_start` as the 32-bit protected-mode entry
- a long-mode capability check
- 4-level bootstrap page-table construction
- `CR3`, `CR4.PAE|PGE`, and `EFER.LME|NXE` programming
- a far jump into `long_mode_entry`
- a higher-half transition through `higher_half`

The ISR assembly path provides:
- exception stubs `isr0..isr31`
- IRQ stubs `irq0..irq15`
- Linux-compat `isr128`
- `swapgs_if_needed`

The context-switch assembly path provides:
- `switch_to`
- `switch_to_first`
- `context_init`
- `fork_return`

## IDT Contract

The x86_64 IDT contains 256 entries using 16-byte long-mode descriptors.
Current initialized vectors include:
- CPU exceptions `0..31`
- legacy IRQ window `32..47`
- Linux-compat `int 0x80`

IST usage is explicit:
- NMI uses `IST_NMI`
- double fault uses `IST_DF`
- machine check uses `IST_MC`

The `int 0x80` gate is present as a user-callable interrupt gate for
compatibility work.

## Syscall Contract

`syscall_init_64()` programs:
- `MSR_LSTAR` with `syscall_entry`
- `MSR_FMASK` to clear `IF`
- `MSR_STAR` with the kernel/user selector contract expected by the x86_64
  syscall entry path

`syscall_handler_64()` currently dispatches through the active personality
attached to `current_process`.

Dispatch semantics:
- out-of-range or missing syscall slots return `-ENOSYS`
- up to six syscall arguments are passed through unchanged
- the return value is staged in `RAX`

## Current Limits

This document does not claim:
- validated long-mode bootstrap on hardware
- validated `sysret` return path
- validated x86_64 scheduler context-switch runtime

Those remain separate bring-up items.

## Verification Status

Covered by host validation:
- `host_test_x86_64_asm`
  - `boot.S`, `isr.S`, and `switch.S` assemble under `cc -m64`
  - required bootstrap, ISR, and context-switch symbols are exported
- `host_test_x86_64_gdt`
  - selector access bytes
  - per-CPU TSS initialization
  - IST population
  - `tss_set_rsp0()` / `tss_get()` behavior
- `host_test_x86_64_idt`
  - exception/IRQ/syscall gate installation
  - IST assignment for critical exceptions
  - exception-name lookup
- `host_test_x86_64_syscall`
  - `LSTAR`, `FMASK`, `STAR` programming contract
  - syscall dispatch through the active personality
  - `-ENOSYS` fallback behavior
