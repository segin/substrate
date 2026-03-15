# i386 VM86 Architecture

## Scope

Substrate exposes an i386 VM86 execution path for BIOS-style real-mode emulation
and BSD-style `sysarch(I386_VM86, ...)` entry. The current implementation is a
minimal monitor-backed design intended for controlled BIOS calls and legacy
instruction emulation, not a full DOS virtual machine.

## Entry Contract

VM86 entry is driven by `sys_vm86()` and the BSD wrapper `vm86_init_bsd()`.
Both copy user-provided state into kernel-owned storage before entering the
assembly handoff path.

`vm86_enter()` constructs a VM86 `iret` frame on the kernel stack and sets:
- `EFLAGS.VM`
- `EFLAGS.IF`
- user `CS:IP`
- user `SS:ESP`
- guest-visible `ES`, `DS`, `FS`, `GS`

The current VM86 ABI uses `struct vm86_struct` and `struct vm86_regs` from
`<sys/vm86.h>`.

## Monitor Model

Each active VM86 session is associated with a lightweight `vm86_monitor`.
Current responsibilities:
- track virtual flags and pending interrupt state
- record whether the session is a kernel BIOS call
- surface unhandled opcode faults to the caller
- capture output registers for kernel BIOS call completion

The monitor is process-global in the current implementation.

## GPF Dispatch Model

The i386 IDT exception path checks for a general-protection fault with
`EFLAGS.VM` set and dispatches that case to `vm86_gpf_handler()` instead of the
normal protected-mode fault path.

`vm86_gpf_handler()` decodes the faulting opcode at guest linear address
`(CS << 4) + IP` and either emulates the instruction or reports an unhandled
fault to the monitor.

## Implemented Opcode Emulation

The current VM86 handler emulates:
- `CLI`
- `STI`
- `PUSHF`
- `POPF`
- `INT n`
- `IRET`
- `IN AL, imm8`
- `OUT imm8, AL`
- `IN AL, DX`
- `OUT DX, AL`
- `HLT` as kernel BIOS-call termination

`INT n` uses the low-memory interrupt vector table at physical address `0` and
pushes a real-mode return frame onto the guest stack.

For currently emulated port I/O instructions, port state is serviced through a
kernel-owned 64 KiB synthetic I/O space used by the VM86 monitor tests and the
minimal BIOS-call path.

## BIOS Call Helper

`vm86_bios_call(int int_no, struct vm86_regs *regs)` provides a small kernel
helper for BIOS interrupt execution.

Current behavior:
- identity maps the first 1 MiB in the kernel pmap
- initializes a monitor in kernel BIOS mode
- writes a stub at low memory containing `INT n; HLT`
- enters VM86 mode
- converts `HLT` into a protected-mode return via `vm86_bios_ret_point`
- copies guest output registers back to the caller

This path is intentionally narrow and is not a generic VM86 multitasking layer.

## TSS I/O Bitmap Contract

The i386 TSS contains an 8192-byte I/O permission bitmap plus the required
terminator byte.

Substrate exports:
- `tss_iomap_init()` to deny all ports
- `tss_set_iomap(port, allow)` to allow or deny one port
- `tss_set_iomap_range(start, end, allow)` to update a contiguous range

Bitmap semantics follow x86 hardware rules:
- bit `0` means allow
- bit `1` means deny and fault

The bitmap lives in the per-CPU TSS and is initialized to deny all ports.

## Current Limits

The current VM86 implementation does not yet provide:
- a full userspace monitor task
- virtual PIC/PIT devices
- broad instruction emulation beyond the explicit opcode set above
- a general DOS-compatible environment

It is sufficient for the current BIOS helper path and bounded VM86 regression
coverage.

## Verification Status

Covered by host validation:
- monitor initialization and fault reporting
- `sys_vm86()` user-state copyin behavior
- BSD `vm86_init_bsd()` wrapper copyin behavior
- `CLI` / `STI`
- `PUSHF` / `POPF`
- `INT n` / `IRET`
- `IN` / `OUT`
- `HLT` BIOS-call termination path
- TSS I/O bitmap initialization and range updates
