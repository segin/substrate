# `usr.bin/ld` Internal Architecture

## Goals
- Link i386/x86-64 ELF objects, archives, and DSOs without backend forwarding.
- Preserve deterministic outputs and parser-friendly diagnostics.
- Provide script, relocation, and dynamic-link feature coverage needed by Substrate userland builds.

## Pipeline
1. Parse CLI into `ld_ctx_t` (mode, policies, scripts, plugins, libraries).
2. Load inputs (`ET_REL`, archives, thin archives, DSO providers).
3. Resolve symbols and unresolved policy.
4. Merge objects with `libelfobj`.
5. Apply section policy (merge, COMDAT, GC, ICF, script placement).
6. Build dynamic artifacts (dynsym/dynstr/dynamic/version/hash, GOT/PLT/TLS).
7. Build segments and assign virtual addresses.
8. Apply relocations and validate output invariants.
9. Emit output, map file, and optional reproduce bundle.

## Core Components
- `load_*`: object/archive/DSO/script-wrapper input handling.
- `symstate_*`: symbol tracking and unresolved set management.
- `apply_gc_sections`, `apply_icf`: reachability and folding passes.
- `add_default_segments`, script PHDR mapping: segment planning.
- `apply_all_relocations`: relocation backend dispatch and overflow checks.
- `write_map_file`, `write_reproduce_bundle`: diagnostics/repro tooling.

## Relocation Backend Model
- Architecture decision by ELF machine (`EM_386`, `EM_X86_64`).
- Backend helpers provide width, signedness, PC-relative interpretation, and apply logic.
- Error reporting includes section name, offset, relocation type, and symbol name.

## Script Engine
- Lexer/parser with include stack and source locations.
- Expression evaluator supports builtins (`ADDR`, `SIZEOF`, `ALIGN`, `LOADADDR`, etc.).
- Semantic handlers include `PROVIDE`, `KEEP`, `/DISCARD/`, `INSERT`, section ordering, and PHDR mapping.

## Determinism and Safety
- Deterministic archive scan behavior and repeated-link reproducibility tests.
- Hard limits for script include depth, symbol tracking, and archive scan passes.
- Frontend fuzz-smoke coverage for malformed object/script inputs.

## Integration Points
- Consumes `libelfobj` APIs for parsing, mutation, merging, and writing.
- Used by `cc` driver and userland build paths (`NATIVE_BUILD=1`) with internal `as`/`ld`.
