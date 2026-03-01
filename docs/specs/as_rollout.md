# as Rollout and Release Gating

## Purpose
This document defines rollout controls for the standalone Substrate `as` implementation (native parser + encoder + ELF writer), including migration strategy, compatibility gates, and regression triage.

## Migration Strategy
- Stage 1: Core frontend (lexer/parser/symbol/section/data directives) with x86 minimum viable encoding path.
- Stage 2: Native x86 ELF output with relocation coverage replacing delegated backend for x86 targets.
- Stage 3: ISA-level expansion for x86-64-v2/v3/v4 plus relaxation hardening.
- Stage 4: ARMv7 backend bring-up (ARM/Thumb + relocations + mapping symbols).
- Stage 5: AArch64 backend bring-up (A64 + v8.1 extensions + relocations).
- Stage 6: Remove delegated backend path or keep only as explicit fallback/debug mode.

## Compatibility Contract During Migration
- Preserve CLI compatibility (`-o`, `-32/-64`, `-march`, `-mtune`, `-I`, `-D`, `-Wa`, `-g`, diagnostics controls).
- Preserve deterministic outputs for identical inputs/options.
- Preserve compatibility with `cc` `-S/-c` pipelines and `ld` linking flows.
- Preserve required ELF metadata invariants (`ET_REL`, class/machine consistency, symbol/relocation correctness).

## Release Criteria and Gating Matrix
- Build: `make -C usr.bin/as` and `make -C usr.bin/as NATIVE_BUILD=1` pass.
- Frontend correctness: lexer/parser/expression tests pass for supported syntax modes.
- Backend correctness: per-architecture encode/relocation test suites pass.
- Toolchain integration: generated objects round-trip through `readelf`/`objdump` and link via Substrate `ld` and GNU `ld` (where applicable).
- ABI checks: relocation types/addends and section/symbol metadata match target ABI requirements.
- Determinism: repeated builds produce byte-identical `.o` outputs.
- Stability: fuzz-smoke suites complete without crashes; controlled error paths remain deterministic.

## Post-Release Regression Triage Process
- Intake: collect source, options, expected output, actual output, `readelf/objdump` diffs, and diagnostics.
- Classification:
  - `frontend-break`: lex/parse/macro/conditional handling regression.
  - `encoding-break`: opcode/prefix/operand encoding mismatch.
  - `reloc-break`: relocation type/addend/target mismatch.
  - `abi-break`: ELF class/machine/type/symbol/section invariant regression.
  - `compat-break`: behavior diverges from required GNU compatibility surface.
  - `determinism-break`: output instability across identical inputs.
  - `perf-break`: throughput/resource regression.
- Reproduction: add/extend regression tests in `tests/usr.bin/as/` before fixing.
- Resolution: patch code + docs + tests together; require green assembler suite before merge.
- Backport priority: `encoding-break`, `reloc-break`, and `abi-break` are highest priority.
