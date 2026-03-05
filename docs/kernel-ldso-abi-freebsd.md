# FreeBSD Personality: Kernel <-> `ld.so` ABI Contract (i386)

This document describes the currently implemented contract between the Substrate kernel and a FreeBSD i386 dynamic linker (`ld-elf.so.1`) during `execve(2)`.

## 1. Personality Selection and Branding

- Personality is selected from ELF `EI_OSABI`.
- `ELFOSABI_FREEBSD` (`9`) selects `PERS_FREEBSD`.
- Other OSABI values default to native Substrate personality.
- As implemented, interpreter ELF load also re-runs personality detection and may overwrite personality.

Practical contract:
- FreeBSD binaries/interpreters must be branded `EI_OSABI=ELFOSABI_FREEBSD`.

## 2. Interpreter Load / Control Transfer

For `PT_INTERP` executables:

- Interpreter path is read from main ELF `PT_INTERP`.
- Interpreter is looked up from `fs_root`.
- Interpreter is loaded at fixed base `0x40000000`.
- CPU jumps to interpreter entrypoint.
- `AT_BASE` is set to `0x40000000`.

Current implemented nuance:
- `AT_ENTRY` follows the final jump target variable, therefore currently becomes interpreter entry when interpreter is present.

## 3. Initial Stack and Auxv Contract

The FreeBSD personality currently receives the same generic ELF stack builder as Linux/native:

- 64 KiB user stack at `0xBFFF0000..0xBFFFFFFF`
- `argc`, `argv[]`, `envp[]`, auxv on initial stack
- auxv keys currently emitted are Linux-style generic keys:
  - `AT_PHDR`, `AT_PHENT`, `AT_PHNUM`, `AT_PAGESZ`, `AT_BASE`, `AT_ENTRY`, `AT_FLAGS`, `AT_RANDOM`, `AT_SECURE`, UID/GID keys, `AT_PLATFORM`, `AT_EXECFN`, `AT_NULL`

Current behavior to account for:
- `AT_SECURE` is always `0`.
- `AT_PLATFORM` is always `"i686"`.
- No FreeBSD-specific auxv enrichment is implemented in this path.

## 4. Syscall ABI Seen by FreeBSD `ld.so`

FreeBSD personality uses stack-based i386 syscall argument passing in this kernel:

- syscall number: `EAX`
- args read from user stack at `ESP+4`, `ESP+8`, ... (kernel ignores `ESP+0`)
- return: `EAX` low 32, `EDX` high 32 for 64-bit values

This is selected in dispatcher by personality name `"FreeBSD"`.

## 5. Loader-Critical Syscall Surface (Current)

Implemented in FreeBSD personality table:
- `open`, `close`, `read`, `write`, `execve`, `lseek`, `fstat/stat/lstat` (FreeBSD11 translations), `poll`, `__getcwd`, basic process/user/group calls.

Major gap for dynamic linker:
- FreeBSD mmap-family entries are not wired in `freebsd_syscalls[]` (e.g. `FREEBSD_SYS_mmap_freebsd13`), even though constants and translation helper exist.
- Without wired `mmap`/`munmap`/`mprotect` path, a FreeBSD runtime linker is not fully operable yet.

## 6. Stability / Non-Goals of Current Contract

- No ASLR for interpreter base or stack.
- Interpreter path resolution uses `fs_root`.
- Generic auxv format is shared with Linux/native instead of FreeBSD-specialized startup metadata.

## Code References

- `sys/exec/formats/elf.c` (interpreter load, stack/auxv)
- `sys/arch/i386/syscall.c` (stack ABI dispatch for `"FreeBSD"`)
- `sys/exec/perso/perso_freebsd.c` (wired syscall table)
- `sys/exec/perso/freebsd/freebsd_syscalls.h` (declared FreeBSD syscall numbers, including `mmap_freebsd13`)
- `sys/exec/perso/compat.c` (`sys_freebsd_mmap` helper exists but is not currently wired into freebsd table)
