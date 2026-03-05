# Syscall ABI And Dispatch

## Native Dispatch Path

- Trap: `int $0x80`
- Entry: `sys/arch/i386/syscall.c::syscall_handler()`
- Dispatch target: personality-specific table (`struct personality`).

## Personality Argument ABI Selection

`syscall_handler()` chooses argument extraction by personality name:

- Stack argument ABI (`args[i] = user_stack[i+1]`):
  - `"substrate"` (native)
  - `"FreeBSD"`
  - `"AT&T UNIX SVR4"`
- Register argument ABI (`ebx, ecx, edx, esi, edi, ebp`):
  - default path (Linux-style personalities)

This means ABI correctness depends on both caller stub and selected personality.

## Return-Value Conventions

- Kernel dispatcher writes return value directly to `EAX` (low 32) and `EDX` (high 32).
- 64-bit returns are supported through `EDX:EAX`.
- Error normalization is not centralized; individual syscalls currently use mixed patterns:
  - negative errno style (e.g., `-EINVAL`)
  - plain `-1` stubs
  - positive errno values in some paths (notably `sys_sysctl`)

## Userspace Callers

### `lib/c`

- Uses stack-based `_syscall0.._syscall6` in `lib/c/arch/i386/syscall.S`.
- This is compatible with native stack ABI dispatch.

### `lib/sys`

- `lib/sys/syscall.S` uses register argument ABI.
- This is compatible with Linux-style personality dispatch.
- For native (`substrate`) stack ABI, this is currently a mismatch and requires remediation.

## Known ABI Gaps

- `SYS_vm86` and `SYS_PROC_PERS_NAME` are hardcoded in `lib/sys` wrappers but are not defined in `sys/arch/i386/syscall.h` and not mapped in native dispatch.
- Several numbered syscalls are defined in `sys/arch/i386/syscall.h` but not wired into native personality dispatch (`SYS_WAITPID`, `SYS_BRK`, `SYS_MUNMAP`, `SYS_CLOCK_GETTIME`, etc.).
