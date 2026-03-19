# `usr.bin/as` Tasklist

Goal: drive `usr.bin/as` from “functionally broad and passing corpora” to a maintainable, fully standalone assembler with explicit parity targets, deterministic validation, and minimal ad hoc emitter logic.

This tasklist is ordered for uninterrupted execution. Work from top to bottom. Do not stop between items unless a listed validation gate fails or another subsystem is the true blocker.

Primary rule: prefer shrinking duplicated logic into shared helpers and lookup tables without changing encoding behavior.

---

## 0. Working Mode

- [ ] Always use a clean native rebuild before validation:
  - `make -C usr.bin/as clean`
  - `make -C usr.bin/as NATIVE_BUILD=1 CC=/usr/bin/cc`
- [ ] Run assembler-only checks after each nontrivial batch:
  - `tests/usr.bin/as/test_x86_64_encoding.sh`
  - `tests/usr.bin/as/test_intel_dual_syntax.sh`
  - `tests/usr.bin/as/test_parser_core.sh`
  - `tests/usr.bin/as/test_x86_32_corpus_intel_roundtrip.sh`
- [ ] Treat `tests/usr.bin/as/test_integration_rollout.sh` failures caused by `usr.bin/ld/ld` separately; do not block assembler cleanup on linker crashes.
- [ ] Commit after each validated batch, with one logical seam per commit.

---

## 1. Finish `as_elf_emit.c` Structural Cleanup

### 1.1 Shared x86 Special-Case Emitters
- [x] Factor duplicated `push`/`pop` segment-register handling shared by i386 and x86_64 emitters.
- [x] Factor duplicated `kand`/`kor`/`kxor`/`kxnor`/`kadd`/`kunpck` operand-selection logic shared by i386 and x86_64 emitters.
- [x] Factor shared `vmread`/`vmwrite` operand-order handling.
- [x] Factor shared immediate-vs-register selector helpers for `shld`/`shrd`, `extrq`/`insertq`, and similar multi-form instructions.

### 1.2 Remaining i386 x87 Cleanup
- [x] Factor `fld` / `fxch` / `fld1` / related x87 single-purpose stack instructions into shared x87 helpers.
- [x] Factor remaining i386 x87 memory-size groups beyond the arithmetic family:
  - `fstp`
  - `ficom*`
  - `fild*`
  - `fist*`
  - `fbld`
  - `fbstp`
- [x] Eliminate remaining i386 x87 mnemonic ladders where opcode selection is table-driven in practice.

### 1.3 Remaining x86_64 x87 Cleanup
- [x] Factor x86_64 x87 exact-memory group:
  - `fcoms`
  - `fcomps`
  - `fcoml`
  - `fcompl`
  - `fstpl`
- [x] Factor x86_64 x87 16/32-bit integer-memory group:
  - `fiadd`
  - `fimul`
  - `ficom`
  - `ficomp`
  - `fisub`
  - `fisubr`
  - `fidiv`
  - `fidivr`
  - `fist`
- [x] Factor x86_64 x87 16/32/64-bit integer-memory group:
  - `fild`
  - `fistp`
  - `fisttp`
- [x] Factor x86_64 `fbld` / `fbstp`.

### 1.4 SSE/MMX Tail Cleanup
- [ ] Remove remaining one-off selector branches that are now expressible via existing lookup helpers.
- [x] Consolidate `movups`/`movupd`/`movaps`/`movapd`/`movlps`/`movhps`/`movlpd`/`movhpd` handling where prefix/opcode patterns match helper form.
- [x] Consolidate `movmskps` / `movmskpd` / `pmovmskb` paths where operand validation is equivalent.
- [x] Consolidate remaining MMX<->XMM bridge instructions into shared helpers.

### 1.5 Control/Cache/Prefetch Tail Cleanup
- [ ] Consolidate prefetch-family reg-field selection into a single lookup path.
- [ ] Consolidate `clflush` / `clwb` / `clflushopt` / `cldemote` related selector handling where possible.
- [ ] Consolidate `movbe`, `movdiri`, `movdir64b`, `enqcmd`, `enqcmds`, `wrssd`, `wrussd`, `aadd`, `aand`, `aor`, `axor` into shared operand-order helpers.

### 1.6 Dead-Path Elimination
- [ ] Remove branches that are now subsumed by lookup helpers.
- [ ] Remove unused local variables introduced by old ladders.
- [ ] Remove duplicate helper code paths that differ only by small selector tables.

Validation gate for Section 1:
- [ ] `usr.bin/as/as_elf_emit.c` builds warning-free under `-Werror`.
- [ ] All assembler-only validation commands in Section 0 pass.

---

## 2. x86 AT&T and Intel Syntax Parity

### 2.1 x86-32 AT&T Corpus
- [ ] Keep `tests/usr.bin/as/corpus/x86_32_gas_all_valid_assembles.s` assembling with no warnings.
- [ ] Keep `tests/usr.bin/as/corpus/x86_32_gas_all_opcodes_assembles.s` assembling with no warnings.
- [ ] Add regression tests for any instruction family whose support depended on emitter refactors.

### 2.2 x86-32 Intel Roundtrip
- [ ] Keep `tests/usr.bin/as/test_x86_32_corpus_intel_roundtrip.sh` green after every emitter cleanup batch.
- [ ] Eliminate any remaining normalization hacks that can be replaced by real parser/encoder support.
- [ ] Add tests for Intel memory-qualifier ambiguity diagnostics.

### 2.3 x86-64 AT&T Corpus
- [ ] Keep x86_64 v1/v2/v3/v4 corpora green.
- [ ] Preserve ISA-level rejection behavior at dispatcher level.
- [ ] Add regression tests whenever a cleanup touches ISA gating.

### 2.4 x86-64 Intel Roundtrip
- [ ] Keep x86_64 Intel roundtrip green for v1/v2/v3/v4.
- [ ] Remove remaining alias-normalization special cases that should be proper encoder behavior.
- [ ] Add explicit tests for `movabs`, `rex.*`, segment-register transfers, and `jrcxz`.

---

## 3. i8086 / Real-Mode Assembler Bring-Up

### 3.1 Corpus Integration
- [ ] Add `tests/usr.bin/as/test_i8086_corpus.sh`.
- [ ] Hook `tests/usr.bin/as/corpus/generate_i8086_gas_corpus.py` outputs into the test matrix.
- [ ] Ensure `.code16` + `.arch i8086` corpora assemble cleanly with our assembler.

### 3.2 Real-Mode Instruction Coverage
- [ ] Implement remaining i8086-specific syntax/encoding gaps discovered by the corpus.
- [ ] Add explicit tests for:
  - far jumps/calls
  - segment overrides
  - 16-bit addressing modes
  - `loop` / `jcxz` / short conditional branches
  - `.org` and fixed-address code16 layout

### 3.3 Real-Mode Output Validation
- [ ] Verify emitted relocatable objects preserve 16-bit intent.
- [ ] Add flat-binary smoke tests once `-O binary` is live.

---

## 4. Intel Syntax Completion

### 4.1 Parser Completeness
- [ ] Audit all Intel memory-size qualifiers against real accepted syntax.
- [ ] Audit ambiguous Intel `mem, imm` diagnostics for consistency.
- [ ] Audit operand-order exceptions:
  - string ops
  - port I/O
  - `movbe`
  - `vmread` / `vmwrite`
  - `movdir64b` / `enqcmd*`

### 4.2 Intel Semantic Parity
- [ ] Add targeted Intel tests for x87 stack forms.
- [ ] Add targeted Intel tests for AVX/AVX2/AVX-512 forms, including mask and broadcast syntax.
- [ ] Add targeted Intel tests for MMX/XMM bridge instructions.

---

## 5. Standalone Assembler Behavior

### 5.1 No Backend Compiler Fallback
- [ ] Audit `usr.bin/as` for any remaining assumptions that external compiler behavior can “fix up” malformed state.
- [ ] Ensure `.s` is treated as preprocessed input.
- [ ] Ensure `.S` preprocessing path goes through `cpp`, not `cc`.
- [ ] Add regression tests to prevent `as -> cc -> as` recursion.

### 5.2 Output Modes
- [ ] Implement and validate `-O binary`.
- [ ] Add tests for flat-binary output layout and section placement.
- [ ] Ensure ELF object emission remains the default and is unaffected by binary mode.

### 5.3 Diagnostics
- [ ] Normalize diagnostics for unsupported mnemonics, operands, and ISA-level failures.
- [ ] Ensure Intel and AT&T syntax errors identify the same root cause cleanly.
- [ ] Add tests for message stability on:
  - unsupported mnemonic
  - bad register class
  - ambiguous memory size
  - displacement/immediate truncation

---

## 6. Directives and Object Semantics

### 6.1 Directive Coverage
- [ ] Audit implemented directives against `docs/specs/as_spec.md`.
- [ ] Add missing directives or tighten diagnostics for unsupported ones.
- [ ] Add tests for:
  - `.section`
  - `.pushsection` / `.popsection`
  - `.group`
  - `.org`
  - `.balign` / `.p2align`
  - `.type` / `.size`
  - `.symver`

### 6.2 Symbol and Relocation Semantics
- [ ] Add tests for local numeric labels in complex mixed forward/backward flows.
- [ ] Add tests for relocation width mismatches and range diagnostics.
- [ ] Add tests for symbol visibility directives and COMDAT/group behavior.

---

## 7. Integration With `ld` and `libelfobj`

### 7.1 Assembler/Linker Boundary
- [ ] Re-enable `tests/usr.bin/as/test_integration_rollout.sh` once `usr.bin/ld` is fixed.
- [ ] Add reduced repros for every assembler/linker contract bug found by the rollout test.
- [ ] Verify:
  - 32-bit and 64-bit relocation records
  - section flags and alignment
  - symbol binding/type metadata

### 7.2 `libelfobj` Contract Validation
- [ ] Add tests ensuring `as` output exercises `libelfobj` without host `elf.h` assumptions.
- [ ] Add tests for section layout and note/metadata preservation in assembler-generated objects.

---

## 8. Architecture Breadth

### 8.1 ARM / AArch64 Cleanup
- [ ] Audit ARM/AArch64 encoder paths for the same “gadgety” selector style still being removed from x86.
- [ ] Factor obvious duplicated opcode selectors there into tables/helpers.
- [ ] Add parity tests comparable to the x86 corpus philosophy.

### 8.2 Host/Target Split
- [ ] Ensure `NATIVE_BUILD=1` paths always use host compiler/linker sanely.
- [ ] Ensure target builds remain free of host-ABI assumptions.
- [ ] Add tests that catch stale-object cross-ABI contamination.

---

## 9. Test Matrix Completion

### 9.1 Required Green Set Before Moving On
- [ ] `tests/usr.bin/as/test_parser_core.sh`
- [ ] `tests/usr.bin/as/test_x86_64_encoding.sh`
- [ ] `tests/usr.bin/as/test_intel_dual_syntax.sh`
- [ ] `tests/usr.bin/as/test_x86_32_corpus_intel_roundtrip.sh`
- [ ] `tests/usr.bin/as/test_x86_64_corpus_intel_roundtrip.sh`
- [ ] `tests/usr.bin/as/test_integration_matrix.sh`
- [ ] i8086 corpus test once added

### 9.2 Regression Additions
- [ ] For every emitter cleanup commit, add or update at least one regression test when the change affects nontrivial dispatch.
- [ ] Keep corpora out of `.gitignore` gaps and keep generated junk out of the repo.

---

## 10. Documentation and Endgame

### 10.1 Spec Alignment
- [ ] Reconcile final implemented behavior with `docs/specs/as_spec.md`.
- [ ] Update spec sections that no longer reflect real behavior.

### 10.2 User Documentation
- [ ] Audit `as(1)` for:
  - Intel syntax behavior
  - `-O binary`
  - ISA-level gating
  - `.S` preprocessing path via `cpp`

### 10.3 Completion Criteria
- [ ] `usr.bin/as/as_elf_emit.c` no longer contains large mnemonic ladders where a lookup/helper is sufficient.
- [ ] x86-32 and x86-64 AT&T corpora remain green.
- [ ] x86-32 and x86-64 Intel roundtrip tests remain green.
- [ ] i8086 corpus is integrated and green.
- [ ] Integration rollout is green once linker-side faults are fixed.
- [ ] Tasklist can be deleted when all boxes are complete.

---

## Recommended Execution Order

1. Section 1
2. Section 2
3. Section 3
4. Section 5
5. Section 6
6. Section 7
7. Section 8
8. Section 10

Do not bounce between distant sections unless a failing validation gate proves the blocker lives elsewhere.
