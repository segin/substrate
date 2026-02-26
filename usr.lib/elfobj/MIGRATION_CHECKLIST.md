# libelfobj Migration Checklist

Use this checklist when replacing ad-hoc ELF handling in other Substrate components.

## Preparation
- Identify all local ELF parsers/writers in the target component.
- Classify each path as read-only parse, object construction, relocation, or link aggregation.
- Add coverage tests before migration (existing behavior snapshots).

## Replacement Steps
- Replace direct ELF header parsing with `elf_open()` / `elf_open_memory()`.
- Replace manual section lookups with `elf_find_section()` and section accessors.
- Replace manual symbol parsing with `elf_find_symbol()`, `elf_symbol_at()`, and hash lookups.
- Replace manual relocation handling with `elf_add_relocation()` and backend APIs.
- Route linker-style merges through `elf_link()` or `elf_link_plan_*` hooks.
- Switch validation gates to `elf_validate_ex()` and structured diagnostics.

## Toolchain Integration
- Ensure produced objects are accepted by local `usr.bin/as` and `usr.bin/ld` flows.
- Validate resulting artifacts with `readelf`, `objdump`, `nm`, and `strip`.
- For debug-heavy outputs, run `elf_debug_validate()` and `test_debug_unwind`-style checks.

## Completion Criteria
- Remove dead ad-hoc parsing code.
- Keep only format-specific logic that is intentionally outside libelfobj scope.
- Update component docs to reference `libelfobj` APIs as the canonical path.
- Add regression tests covering migrated behavior.
