# Substrate Native Personality: Kernel <-> `ld.so` ABI Contract (i386)

This document describes the native Substrate ABI contract between kernel `execve` and a native dynamic linker.

## 1. Personality Selection

- Native personality is selected when `EI_OSABI` is neither Linux (`3`) nor FreeBSD (`9`).
- Project-specific OSABI constant is `ELFOSABI_SUBSTRATE` (`64`).

## 2. Interpreter Load / Control Transfer

If the executable has `PT_INTERP`:

- Kernel reads interpreter path from `PT_INTERP`.
- Loads interpreter from `fs_root` at fixed base `0x40000000`.
- Transfers control directly to interpreter entrypoint.
- Sets `AT_BASE` to interpreter base.

Current implemented nuance:
- `AT_ENTRY` is currently filled from the same final entry variable used for jump, so with an interpreter it reflects interpreter entry.

## 3. Initial Stack and Auxv Contract

Native uses the shared ELF stack builder (same as Linux/FreeBSD personalities):

- User stack region: `0xBFFF0000..0xBFFFFFFF` (64 KiB)
- `argc` at `%esp`, followed by `argv[]`, NULL, `envp[]`, NULL, auxv
- auxv keys currently emitted:
  - `AT_PHDR`, `AT_PHENT`, `AT_PHNUM`, `AT_PAGESZ`, `AT_BASE`, `AT_ENTRY`, `AT_FLAGS`
  - `AT_RANDOM` (16 bytes)
  - `AT_SECURE` (always 0)
  - `AT_UID`, `AT_EUID`, `AT_GID`, `AT_EGID`
  - `AT_PLATFORM` -> `"i686"`
  - `AT_EXECFN` -> `argv[0]`
  - terminator `AT_NULL`

`AT_PHDR` behavior:
- Derived from `PT_PHDR` or matching `PT_LOAD` covering `e_phoff`; fallback guess used if not found.

## 4. Native Syscall ABI for `ld.so`

Native personality syscall ABI is stack-based via `int $0x80`:

- syscall number in `EAX`
- kernel consumes arguments from user stack starting at `ESP+4`
- call stubs place a dummy return slot at `ESP+0`
- return low 32 in `EAX`, high 32 in `EDX` for 64-bit values

This is what native libc `_syscallN` stubs implement.

## 5. Loader-Critical Syscall Surface (Current)

Native personality table includes:
- `mmap`, file/metadata syscalls, signal syscalls, and thread syscalls.

Current gap to design around:
- Native syscall table does not currently expose explicit `munmap`, `brk`, or `mprotect` entries.

## 6. TLS Contract

At initial user entry, kernel sets segment selectors:
- `DS/ES/FS = 0x23`
- `GS = 0x33` (TLS slot selector)

For native personality, `modify_ldt` is available in syscall table. `set_thread_area` is implemented for Linux personality use.

## 7. Stability / Non-Goals of Current Contract

- No ASLR for interpreter base or initial stack placement.
- Interpreter lookup currently uses global `fs_root`.
- `AT_SECURE` is not computed from privilege transition; it is currently hardcoded to 0.

## Code References

- `sys/exec/formats/elf.c` (OSABI selection, interpreter mapping, stack/auxv)
- `sys/exec/formats/elf.h` (`ELFOSABI_SUBSTRATE`, `AT_*` constants)
- `sys/arch/i386/syscall.c` (native stack-argument dispatch)
- `lib/c/arch/i386/syscall.S` (native stack-form syscall stubs)
- `sys/exec/perso/perso_native.c` (native syscall table)
- `sys/arch/i386/isr.S` (initial userspace selector setup including `GS=0x33`)
