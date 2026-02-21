# ld Specification

## Scope
`ld` is a linker driver for i386 and x86_64.
It delegates actual link work to host `ld` with explicit emulation mode and then
verifies output ABI metadata via `libelfobj`.

## Pipeline
1. Parse CLI and collect options/inputs.
2. Determine link mode (`-m32`/`-m64`, else infer from first ELF input).
3. Invoke host `ld -m elf_i386|elf_x86_64` with forwarded options and inputs.
4. Validate produced output class/machine/type via `libelfobj`.

## Error Model
- Input/parse failures are fatal.
- Backend linker failure is fatal.
- Output ELF class/machine/type mismatch is fatal.

## ABI/Compatibility Notes
- `ld` supports complete practical linker semantics available from host `ld`
  because it forwards options/inputs directly.
- `libelfobj` is used as a verification layer for resulting ELF ABI shape rather
  than for relocation/layout synthesis in this driver.
