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
- [ ] `.text`, `.data`, `.bss` section switching.
- [ ] `.section` with flags/type metadata.
- [ ] Symbol bindings: local/global/weak.
- [ ] Symbol visibility controls and type/size metadata.
- [ ] Absolute vs relocatable expression classification.
- [ ] Expression folding with width/sign correctness.
- [ ] Overflow diagnostics for immediate and displacement expressions.
- [ ] Symbol redefinition and multiply-defined diagnostics.
- [ ] `.comm` / `.lcomm` semantics.
- [ ] Alignment directives (`.align`, `.p2align`, `.balign`) semantics.

## 4) Instruction Encoding: i386 Baseline
- [ ] Integer ALU instruction families.
- [ ] Control flow (`jmp`, `jcc`, `call`, `ret`) with relaxation.
- [ ] Stack/frame instructions.
- [ ] Data movement variants including sign/zero extension.
- [ ] Shift/rotate, bit test/manipulation groups.
- [ ] x87 baseline instructions needed by toolchain/runtime.
- [ ] SSE/SSE2 baseline coverage.
- [ ] Prefix handling (`lock`, `rep`, segment overrides).
- [ ] Addressing modes: base/index/scale/displacement, absolute, RIP-absent i386 forms.
- [ ] Invalid operand form diagnostics with source location.

## 5) Instruction Encoding: x86_64 Baseline
- [ ] REX prefix generation and validation.
- [ ] 64-bit operand/address-size semantics.
- [ ] RIP-relative addressing support.
- [ ] SysV AMD64 call/jump relocation forms.
- [ ] SSE2+ scalar/vector baseline used by compiler output.
- [ ] TLS access pattern instruction forms.
- [ ] PLT/GOT access pattern instruction forms.
- [ ] ISA feature gating by `-march`.
- [ ] Mode-specific forbidden instruction diagnostics.
- [ ] Stable encoding choices for reproducible output.

## 6) Relocations and ELF Emission
- [ ] Emit ET_REL ELF32 for i386 mode.
- [ ] Emit ET_REL ELF64 for x86_64 mode.
- [ ] i386 relocation set (`R_386_*`) required by `ld`.
- [ ] x86_64 relocation set (`R_X86_64_*`) required by `ld`.
- [ ] REL/RELA handling per target ABI requirements.
- [ ] Addend handling correctness.
- [ ] Section-relative and symbol-relative relocation encoding.
- [ ] Relocation overflow checks and deterministic failure.
- [ ] String table and symbol table construction.
- [ ] Section header layout/alignment correctness.

## 7) Directives and Data Emission
- [ ] Data directives (`.byte/.short/.long/.quad`) endianness/width correctness.
- [ ] String directives (`.ascii/.asciz/.string`) behavior.
- [ ] Space/zero-fill directives (`.space/.fill/.zero`) behavior.
- [ ] Org/location-counter directives where supported.
- [ ] `.type` / `.size` semantics for functions/objects.
- [ ] `.file` / `.loc` integration points for debug info.
- [ ] `.cfi_*` directive parsing and frame info emission.
- [ ] Note section directives where needed.
- [ ] TLS section directives.
- [ ] COMDAT/group directive handling.

## 8) GNU and Toolchain Compatibility Surface
- [ ] GNU local label conventions used by compiler output.
- [ ] Compiler-emitted `.cfi_*` directive set compatibility.
- [ ] Compiler-emitted `.section` flag syntaxes compatibility.
- [ ] GCC/Clang generated inline-asm patterns compatibility.
- [ ] `.intel_syntax` and `.att_syntax` transitions.
- [ ] Relaxation behavior compatibility expectations.
- [ ] Feature-guarded compatibility matrix by `-march`.
- [ ] Compatibility tests against known GNU as output patterns.
- [ ] Intentional incompatibilities documented and tested.
- [ ] Driver output compatibility with `ld` and `objdump`.

## 9) Diagnostics, Safety, and Determinism
- [ ] `file:line:col` diagnostics for parse/encode errors.
- [ ] Include stack trace in diagnostics.
- [ ] Expression evaluation context in overflow diagnostics.
- [ ] Bounds checks for all section/data buffer writes.
- [ ] Configurable hard limits (macro depth, include depth, token length).
- [ ] Reproducible object output under identical inputs/options.
- [ ] No host-dependent ordering of symbols/sections.
- [ ] Graceful OOM handling with deterministic failure.
- [ ] Fuzz-hardening for parser and expression engine.
- [ ] Structured internal error codes for programmatic use.

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
