# ld.x86 Specification

## Scope
`ld.x86` links ELF relocatable objects for i386 and x86_64 and emits linked ELF output.

Current implemented scope in-tree:
- Object ingestion for `.o` and archive (`.a`) members containing ELF.
- Multi-object merge using `libelfobj` (`elf_link`).
- Output mode selection: `ET_REL`, `ET_EXEC`, `ET_DYN` header type.
- CLI options: `-r`, `-shared`, `-pie`, `-static`, `-o`, `-L`, `-l`, `-T`,
  `--gc-sections`, `--strip-all`, `--allow-undefined`.

Planned scope (design target):
- Full relocation application for final links.
- GOT/PLT synthesis.
- Symbol versioning.
- Linker script execution (`SECTIONS`, `MEMORY`, `ENTRY`, `PROVIDE` subset).
- Dead code elimination and section GC.

## Pipeline
1. Parse CLI and collect inputs.
2. Resolve `-l` against `-L` paths and system defaults.
3. Open ELF objects directly (`elf_open`) or from archive members (`elf_open_memory`).
4. Merge through `elf_link`.
5. Set output ELF type (`elf_set_type`) according to mode.
6. Validate (`elf_validate`) and write (`elf_write_file`).

## Error Model
- Input/parse failures are fatal.
- Archive corruption is fatal.
- ELF format/link failures propagate `elf_err_t` diagnostics.

## ABI/Compatibility Notes
- Produced output is section-based and currently does not build final program headers or GOT/PLT.
- `-shared`, `-pie`, `-static` currently affect ELF header type (`e_type`) only.
- `-r` is the fully supported mode for incremental relocatable output.

