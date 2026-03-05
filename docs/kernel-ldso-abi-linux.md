# Linux Personality: Kernel <-> `ld.so` ABI Contract (i386)

This document describes the contract currently implemented in-tree between the Substrate kernel and a Linux i386 dynamic linker (`ld.so` / `ld-linux.so.2`) during `execve(2)`.

## 1. Personality Selection and Branding

- Personality is selected from `EI_OSABI` during ELF load.
- `ELFOSABI_LINUX` (`3`) selects `PERS_LINUX`.
- Any non-FreeBSD/non-Linux OSABI defaults to native Substrate personality.
- This selection runs for both the main executable and the interpreter image; the second load can overwrite `current_process->perso_id`.

Practical contract:
- Linux binaries and their interpreter should be branded with `EI_OSABI=ELFOSABI_LINUX`.

## 2. Interpreter Load / Control Transfer

If `PT_INTERP` is present in the main executable:

- Kernel reads the interpreter path from `PT_INTERP`.
- Interpreter is looked up via `vfs_lookup(fs_root, interp_path)`.
- Interpreter is loaded at fixed base `0x40000000`.
- Final userspace entry (`EIP`) is the interpreter entrypoint.
- `AT_BASE` is set to interpreter base (`0x40000000`).

Current implemented nuance:
- `AT_ENTRY` is populated from the same `entry` variable used for final jump, so with `PT_INTERP` it currently carries interpreter entry, not main executable entry.

## 3. Initial User Stack Contract

User stack mapping:
- 16 pages (64 KiB) at `0xBFFF0000..0xBFFFFFFF`.
- Initial stack cursor starts from `0xBFFFFFFC`.

Stack content (high to low while building):
- env strings, argv strings
- auxv payload strings/random bytes
- auxv pairs
- `envp[]`, NULL
- `argv[]`, NULL
- `argc`

Auxv keys currently emitted:
- `AT_NULL`
- `AT_ENTRY`
- `AT_PHNUM`
- `AT_PHENT`
- `AT_PHDR`
- `AT_PAGESZ=4096`
- `AT_FLAGS=0`
- `AT_BASE`
- `AT_RANDOM` (16 random bytes)
- `AT_SECURE=0`
- `AT_UID`, `AT_GID`, `AT_EUID`, `AT_EGID`
- `AT_PLATFORM` -> `"i686"`
- `AT_EXECFN` -> `argv[0]` (or 0 if absent)

Not currently emitted:
- `AT_SYSINFO`, `AT_SYSINFO_EHDR`, `AT_HWCAP`, `AT_CLKTCK`, Linux-specific extras.

## 4. Syscall ABI Seen by Linux `ld.so`

Entry instruction:
- `int $0x80`

Register contract for `PERS_LINUX`:
- syscall number: `EAX`
- args: `EBX`, `ECX`, `EDX`, `ESI`, `EDI`, `EBP`
- return: low 32 in `EAX`, high 32 in `EDX` for 64-bit returns

## 5. Loader-Critical Syscall Surface (Current)

Linux personality table wires:
- `open`, `close`, `read`, `write`, `lseek`, `fstat/stat/lstat` variants
- `mmap`, `mmap2`, `munmap`, `brk`
- `set_thread_area` (TLS), `futex`
- `rt_sigaction`, `rt_sigprocmask`, `sigreturn` handlers

Important gap:
- No Linux `mprotect` syscall entry is currently wired in the Linux personality table.

## 6. TLS Contract

At first userspace entry, kernel sets `GS=0x33` (TLS selector slot).
Linux loader/libc is expected to call `set_thread_area(2)`:

- `entry_number=-1` allocates first TLS slot (`GDT` entry 6).
- Allowed TLS entries are 6..8.
- Kernel installs descriptor, loads `GS`, and updates saved trapframe `GS` so return path preserves TLS base.

## 7. Stability / Non-Goals of Current Contract

- No ASLR for interpreter base or initial stack.
- `AT_SECURE` hardcoded to 0.
- Interpreter lookup currently uses global `fs_root`, not per-process root.

## Code References

- `sys/exec/formats/elf.c` (OSABI select, interpreter load, auxv, stack setup)
- `sys/exec/formats/elf.h` (OSABI and `AT_*` constants)
- `sys/arch/i386/syscall.c` (Linux register syscall ABI, `set_thread_area`)
- `sys/arch/i386/isr.S` (user entry sets `GS=0x33`)
- `sys/exec/perso/perso_linux.c` (Linux syscall table wiring)
