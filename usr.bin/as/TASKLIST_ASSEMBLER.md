# `usr.bin/as` Comprehensive Tasklist

Purpose: long-term actionable checklist for a production assembler for i386 and x86_64, with deterministic ELF output and tight `cc`/`ld` integration.

Scope:
- Assembler CLI, parser, expression engine, encoding, relocations, diagnostics.
- ELF emission policy and ABI correctness.
- GNU compatibility surface needed for real-world software.

Execution policy:
- Complete one checkbox at a time.
- Add regression tests before marking done.
- Verify both `-32` and `-64`.
- Keep correctness and diagnostics ahead of performance tuning.

---

## 1) Core CLI and Driver Semantics
- [x] `as -o file.o file.s` baseline behavior.
- [x] `-32` and `-64` mode forcing.
- [x] Target inference when mode flag is absent.
- [x] `-g` debug generation path.
- [x] `-I` include directory handling.
- [x] `-Dname[=value]` symbol predefine handling.
- [x] `-Wa,` pass-through interoperability for compiler driver use.
- [x] `-march` feature-level handling with diagnostics on unsupported levels.
- [x] `-mtune` acceptance and metadata plumbing.
- [x] Deterministic argument parsing and stable diagnostics ordering.

## 2) Lexing, Parsing, and Assembly Source Model
- [x] Tokenization for labels, mnemonics, operands, directives, comments.
- [x] Numeric literal forms (dec/oct/hex/bin where supported).
- [x] String and character literal escape handling.
- [x] Local label forms and forward/backward reference handling.
- [x] Statement grammar for AT&T syntax.
- [x] Statement grammar for Intel syntax (selectable mode).
- [x] Robust recovery from syntax errors to emit multiple diagnostics.
- [x] Include stack tracking and cycle detection.
- [x] Macro definition/expansion (`.macro`/`.endm`) with argument substitution.
- [x] Conditional assembly (`.if`/`.ifdef`/`.ifndef`/`.else`/`.endif`).

## 3) Section, Symbol, and Expression Semantics
- [x] `.text`, `.data`, `.bss` section switching.
- [x] `.section` with flags/type metadata.
- [x] Symbol bindings: local/global/weak.
- [x] Symbol visibility controls and type/size metadata.
- [x] Absolute vs relocatable expression classification.
- [x] Expression folding with width/sign correctness.
- [x] Overflow diagnostics for immediate and displacement expressions.
- [x] Symbol redefinition and multiply-defined diagnostics.
- [x] `.comm` / `.lcomm` semantics.
- [x] Alignment directives (`.align`, `.p2align`, `.balign`) semantics.

## 4) Instruction Encoding: i386 Baseline
- [x] Integer ALU instruction families.
- [x] Control flow (`jmp`, `jcc`, `call`, `ret`) with relaxation.
- [x] Stack/frame instructions.
- [x] Data movement variants including sign/zero extension.
- [x] Shift/rotate, bit test/manipulation groups.
- [x] x87 baseline instructions needed by toolchain/runtime.
- [x] SSE/SSE2 baseline coverage.
- [x] Prefix handling (`lock`, `rep`, segment overrides).
- [x] Addressing modes: base/index/scale/displacement, absolute, RIP-absent i386 forms.
- [x] Invalid operand form diagnostics with source location.

## 5) Instruction Encoding: x86_64 Baseline
- [x] REX prefix generation and validation.
- [x] 64-bit operand/address-size semantics.
- [x] RIP-relative addressing support.
- [x] SysV AMD64 call/jump relocation forms.
- [x] SSE2+ scalar/vector baseline used by compiler output.
- [x] TLS access pattern instruction forms.
- [x] PLT/GOT access pattern instruction forms.
- [x] ISA feature gating by `-march`.
- [x] Mode-specific forbidden instruction diagnostics.
- [x] Stable encoding choices for reproducible output.

## 6) Relocations and ELF Emission
- [x] Emit ET_REL ELF32 for i386 mode.
- [x] Emit ET_REL ELF64 for x86_64 mode.
- [x] i386 relocation set (`R_386_*`) required by `ld`.
- [x] x86_64 relocation set (`R_X86_64_*`) required by `ld`.
- [x] REL/RELA handling per target ABI requirements.
- [x] Addend handling correctness.
- [x] Section-relative and symbol-relative relocation encoding.
- [x] Relocation overflow checks and deterministic failure.
- [x] String table and symbol table construction.
- [x] Section header layout/alignment correctness.

## 7) Directives and Data Emission
- [x] Data directives (`.byte/.short/.long/.quad`) endianness/width correctness.
- [x] String directives (`.ascii/.asciz/.string`) behavior.
- [x] Space/zero-fill directives (`.space/.fill/.zero`) behavior.
- [x] Org/location-counter directives where supported.
- [x] `.type` / `.size` semantics for functions/objects.
- [x] `.file` / `.loc` integration points for debug info.
- [x] `.cfi_*` directive parsing and frame info emission.
- [x] Note section directives where needed.
- [x] TLS section directives.
- [x] COMDAT/group directive handling.

## 8) GNU and Toolchain Compatibility Surface
- [x] GNU local label conventions used by compiler output.
- [x] Compiler-emitted `.cfi_*` directive set compatibility.
- [x] Compiler-emitted `.section` flag syntaxes compatibility.
- [x] GCC/Clang generated inline-asm patterns compatibility.
- [x] `.intel_syntax` and `.att_syntax` transitions.
- [x] Relaxation behavior compatibility expectations.
- [x] Feature-guarded compatibility matrix by `-march`.
- [x] Compatibility tests against known GNU as output patterns.
- [x] Intentional incompatibilities documented and tested.
- [x] Driver output compatibility with `ld` and `objdump`.

## 9) Diagnostics, Safety, and Determinism
- [x] `file:line:col` diagnostics for parse/encode errors.
- [x] Include stack trace in diagnostics.
- [x] Expression evaluation context in overflow diagnostics.
- [x] Bounds checks for all section/data buffer writes.
- [x] Configurable hard limits (macro depth, include depth, token length).
- [x] Reproducible object output under identical inputs/options.
- [x] No host-dependent ordering of symbols/sections.
- [x] Graceful OOM handling with deterministic failure.
- [x] Fuzz-hardening for parser and expression engine.
- [x] Structured internal error codes for programmatic use.

## 10) Testing, Fuzzing, and Performance
- [ ] Unit tests: lexer/parser/expression engine.
- [ ] Unit tests: relocation generation and edge overflow.
- [ ] Golden tests: known `.s` -> `.o` byte/metadata checks.
- [ ] Round-trip tests with `objdump`/`readelf` verification.
- [ ] Integration tests with `cc` generated assembly.
- [ ] Integration tests with `ld` linking real programs.
- [ ] Differential tests vs GNU as for selected corpora.
- [ ] Fuzzing harnesses for parser and directive handling.
- [ ] Large-file throughput benchmarks.
- [ ] Regression suite for deterministic output stability.

## 11) Integration and Rollout
- [ ] Makefile/build integration in recursive tree.
- [ ] Host build and target build behavior validation.
- [ ] `cc` driver integration for `-S`/`-c` pipelines.
- [ ] Native Linux host smoke tests for produced objects.
- [ ] Substrate target smoke tests for produced objects.
- [ ] Man page and user docs alignment with actual behavior.
- [ ] Migration checklist from delegated backend path to full native encoder.
- [ ] ABI contract tests for generated ELF metadata.
- [ ] Release criteria and gating matrix documented.
- [ ] Post-release regression triage process defined.
