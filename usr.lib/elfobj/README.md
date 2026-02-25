# libelfobj

`libelfobj` is a production-oriented ELF object handling library for Substrate tools.

## Scope
- Parse ELF32/ELF64 in little-endian and big-endian modes.
- Support ET_REL, ET_EXEC, ET_DYN, and read-only ET_CORE parsing.
- Build/modify sections, symbols, and relocations.
- Emit valid ELF objects with generated string/symbol/relocation tables.
- Provide linker-facing object merge API and validation diagnostics.
- Parse and validate program headers, notes, dynamic, and versioning sections.
- Provide section mutation/reorder APIs and explicit segment mapping helpers.
- Provide symbol versioning metadata, deterministic symbol ordering, and hash lookups.

## Build
- `make -C usr.lib/elfobj`
- `make -C usr.lib/elfobj test NATIVE_BUILD=1`

## Install
- `make -C usr.lib/elfobj install DESTDIR=...`

## API
Public API is defined in `include/elfobj.h`.

Lifecycle:
- `elf_open` / `elf_open_memory` create read-only objects.
- `elf_create` creates mutable objects.
- `elf_finalize` freezes object state.
- `elf_write_file` finalizes mutable objects automatically.
- `elf_close` releases all resources.

Thread-safety and reentrancy:
- Independent `elfobj_t` instances are reentrant and safe for concurrent use.
- A single `elfobj_t` requires external synchronization for concurrent mutation.
- Relocation backend registration is serialized internally.

## Stability
`libelfobj` uses opaque handles and an error-code based ABI intended for long-term stability.
See `usr.lib/elfobj/ABI_POLICY.md` and `usr.lib/elfobj/libelfobj.map`.
