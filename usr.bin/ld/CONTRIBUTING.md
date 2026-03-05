# Contributing to `usr.bin/ld`

## Adding a New Relocation Type
1. Add machine/type handling in relocation helpers used by `apply_all_relocations`.
2. Define width/range/PC-relative semantics and overflow behavior.
3. Ensure both REL/RELA addend paths are correct for target architecture.
4. Add targeted tests under `tests/usr.bin/ld/`:
   - success case with expected value
   - overflow failure with diagnostic context
   - unresolved-symbol failure path (if applicable)
5. Update requirement tag mapping in `tests/usr.bin/ld/run_all.sh`.

## Adding a New Linker Script Directive
1. Extend lexer/parser token handling with source-location diagnostics.
2. Add/extend AST representation if directive is stateful.
3. Implement evaluator/semantic stage behavior (and ordering interactions).
4. Add script corpus case under `tests/usr.bin/ld/corpus/scripts/`.
5. Add dedicated regression script and requirement tags.

## Diagnostic Rules
- Keep first line parser-friendly (`ld: error: ...` or `ld: warning: ...`).
- Add structured note metadata (category/source/hint) where useful.
- Include object/section/symbol provenance in failures when available.

## Reproducibility Rules
- Maintain deterministic iteration order in emitted diagnostics/maps.
- Keep `test_deterministic_repro.sh` and `test_reproduce_bundle.sh` passing.
- Avoid backend forwarding to external `ld` implementations.

## Validation Before Commit
- `make -C usr.bin/ld NATIVE_BUILD=1`
- `tests/usr.bin/ld/run_all.sh`
