# libelfobj ABI Policy

This library follows a strict, versioned ABI policy.

## Current ABI

- ABI generation: `ELFOBJ_1.0`
- Public API version macro: `ELFOBJ_API_VERSION` (currently `1`)
- Symbol version script: `usr.lib/elfobj/libelfobj.map`

## Stability Rules

- Public handles remain opaque (`elfobj_t`, `elf_section_t`, `elf_symbol_t`, `elf_reloc_t`).
- Existing exported function signatures are never changed in place.
- New APIs are additive and introduced under a newer symbol version when required.
- Error code numeric values in `elf_err_t` are stable once released.
- Existing behavior is preserved unless documented as a bug fix.

## Lifecycle Contract

- Objects from `elf_open()` / `elf_open_memory()` are read-only views.
- Objects from `elf_create()` are mutable until `elf_finalize()`.
- `elf_finalize()` freezes object layout and further mutation fails with `ELF_ERR_STATE`.
- `elf_write_file()` finalizes mutable objects before writing.
- Read-only untouched objects are written byte-for-byte from original image.

## Compatibility Guardrails

- `tests/test_abi_surface.sh` enforces symbol/API surface consistency against
  `tests/abi_api_v1.txt`.
- Any intentional API changes must update:
  - `include/elfobj.h`
  - `usr.lib/elfobj/libelfobj.map`
  - `usr.lib/elfobj/tests/abi_api_v1.txt` (or add a new version file)
  - manual pages under `man/man3/`
