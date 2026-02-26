# libelfobj Compatibility Matrix

## Target/Format Coverage

| ELF Class | Endian | ET_REL | ET_EXEC | ET_DYN | ET_CORE |
| --- | --- | --- | --- | --- | --- |
| ELF32 | Little | Read/Write | Read/Write | Read/Write | Read-only |
| ELF32 | Big | Read/Write | Read/Write (container-level) | Read/Write (container-level) | Read-only |
| ELF64 | Little | Read/Write | Read/Write | Read/Write | Read-only |
| ELF64 | Big | Read/Write | Read/Write (container-level) | Read/Write (container-level) | Read-only |

Notes:
- ET_CORE support is parser/validation only; write path is not enabled for core dumps.
- Big-endian ET_EXEC/ET_DYN support is validated at ELF container level with section/symbol/segment metadata handling.

## ABI Supplement Checks

Implemented and tested for:
- i386 SysV psABI relocation sizing and PC-relative rules.
- x86_64 SysV psABI relocation sizing and PC-relative rules.
- Relocation arithmetic conformance checks via `elf_apply_relocation_value()`.

## Test Mapping

`usr.lib/elfobj/tests/test_matrix.c` validates:
- ELF32 LE/BE ET_REL round-trip.
- ELF64 LE/BE ET_REL round-trip.
- ET_EXEC and ET_DYN read/write flows.
- ET_CORE read-only parsing contract.
- i386/x86_64 relocation conformance spot checks.
