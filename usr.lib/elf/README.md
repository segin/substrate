# libelfobj

`libelfobj` is a production-oriented ELF object handling library for Substrate tools.

## Scope
- Parse ELF32/ELF64 in little-endian and big-endian modes.
- Support ET_REL, ET_EXEC, ET_DYN, and read-only ET_CORE parsing.
- Build/modify sections, symbols, and relocations.
- Emit valid ELF objects with generated string/symbol/relocation tables.
- Provide linker-facing object merge API and validation diagnostics.

## Build
- `make -C usr.lib/elf`
- `make -C usr.lib/elf test NATIVE_BUILD=1`

## Install
- `make -C usr.lib/elf install DESTDIR=...`

## API
Public API is defined in `include/elfobj.h`.

## Stability
`libelfobj` uses opaque handles and an error-code based ABI intended for long-term stability.
