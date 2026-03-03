# `usr.bin/ld` Full Parity Tasklist (lld/gold Replacement)

Source of truth requirements: `usr.bin/ld/SPEC.md`  
Execution rule: complete one checkbox at a time; each completed item must include tests and requirement linkage.

---

## 0. Program Controls and Definition of Done

- [x] Create `tests/usr.bin/ld/README.md` with test taxonomy and required naming conventions.  
  Reqs: LD-U-007, LD-R-004. Stories: US-301.
- [x] Add requirement/story tag format to commit template for linker work.  
  Reqs: Traceability section. Stories: US-301.
- [x] Establish pass/fail dashboard script (`tests/usr.bin/ld/run_all.sh`) with summary by requirement ID.  
  Reqs: LD-U-010. Stories: US-301.

---

## 1. Driver and Option Compatibility

### 1.1 Core invocation and mode selection
- [x] Normalize default mode policy (x86-64 default, i386 explicit).  
  Reqs: LD-U-002, LD-U-003.
- [x] Implement strict parser for `-m`, `-m32`, `-m64`, including canonical aliases and diagnostics.  
  Reqs: LD-U-010, LD-E-007.
- [x] Add unsupported-option policy layer: warn vs error matrix by compatibility mode.  
  Reqs: LD-U-010, LD-W-003.

### 1.2 Essential GNU-compatible options
- [x] Implement `-o`, `-e`, `--entry`, `-r`, `-shared`, `-pie`, `-static`.  
  Reqs: LD-U-001, LD-U-009. Stories: US-001, US-101.
- [ ] Implement `-L`, `-l`, `-Bstatic`, `-Bdynamic` with ordered search semantics.  
  Reqs: LD-U-004. Stories: US-002.
- [ ] Implement `--start-group/--end-group` and `--whole-archive/--no-whole-archive`.  
  Reqs: LD-S-001, LD-U-004. Stories: US-002.
- [ ] Implement `--as-needed/--no-as-needed`.  
  Reqs: LD-E-005. Stories: US-201.

### 1.3 Tooling options
- [ ] Implement `-Map`, `--trace`, `--trace-symbol`, `--version`, `--help`.  
  Reqs: LD-U-011, LD-U-010. Stories: US-301.
- [ ] Implement warning policy options (`--fatal-warnings`, `--warn-common`, unresolved policies).  
  Reqs: LD-W-003, LD-E-001.
- [ ] Add regression tests for every option in this section.  
  Reqs: LD-U-007.

---

## 2. Input Format Support

### 2.1 ET_REL input loader
- [ ] Validate ELF class/machine/endianness and reject incompatible inputs.  
  Reqs: LD-U-012, LD-R-001.
- [ ] Parse symbols, relocations, groups, and section metadata needed for linking.  
  Reqs: LD-U-005, LD-U-006.
- [ ] Add malformed-object hardening tests.  
  Reqs: LD-R-001, LD-R-003, LD-R-004.

### 2.2 Archive loader
- [ ] Implement robust parser for GNU/BSD archives, including symbol tables.  
  Reqs: LD-U-004, LD-R-001.
- [ ] Implement thin archive resolution with path canonicalization and safety checks.  
  Reqs: LD-U-004, LD-R-002.
- [ ] Implement lazy extraction engine driven by unresolved symbol set.  
  Reqs: LD-S-001, LD-E-001. Stories: US-002.
- [ ] Add archive fuzz corpus and parser sanitization tests.  
  Reqs: LD-R-004.

### 2.3 Shared object input support
- [ ] Parse ET_DYN inputs as import-only providers (`.dynsym/.dynstr`).  
  Reqs: LD-U-004, LD-U-005.
- [ ] Integrate DSO symbol candidates into resolver with visibility/version awareness.  
  Reqs: LD-U-005, LD-S-003.
- [ ] Implement DT_NEEDED planning with `--as-needed` gate.  
  Reqs: LD-E-005. Stories: US-201.

---

## 3. Symbol Resolution Engine

### 3.1 Global resolution rules
- [ ] Implement complete binding precedence (strong/weak/common/undef).  
  Reqs: LD-U-005, LD-E-003.
- [ ] Implement `SHN_COMMON` placement policy and conflict handling.  
  Reqs: LD-U-005.
- [ ] Implement unresolved handling matrix by output type and policy flags.  
  Reqs: LD-E-001, LD-E-002.

### 3.2 Visibility and versioning
- [ ] Enforce visibility semantics (`DEFAULT/HIDDEN/PROTECTED/INTERNAL`) in resolver.  
  Reqs: LD-U-005.
- [ ] Implement symbol version definitions and references (`.gnu.version*`).  
  Reqs: LD-S-003. Stories: US-201.
- [ ] Implement `--defsym`, `--undefined`, export-dynamic controls.  
  Reqs: LD-U-005, LD-E-001.

### 3.3 Diagnostics quality
- [ ] Emit unresolved and duplicate diagnostics with defining/reference provenance.  
  Reqs: LD-U-010, LD-E-001, LD-E-003. Stories: US-102.
- [ ] Add symbol-resolution differential tests vs GNU ld and lld for mixed weak/common cases.  
  Reqs: LD-U-007.

---

## 4. Relocation Backends (x86-64 and i386 complete)

### 4.1 Generic relocation framework
- [ ] Formalize backend API (`size`, `is_pc_rel`, `apply`, overflow checks).  
  Reqs: LD-U-006, LD-R-003.
- [ ] Ensure addend extraction rules for REL vs RELA are architecture-correct.  
  Reqs: LD-U-006.
- [ ] Add precise relocation error context (section+offset+symbol+type).  
  Reqs: LD-E-006, LD-U-010.

### 4.2 x86-64
- [ ] Complete ABS/PC/GOT/PLT/TLS/IFUNC reloc family support used by GCC/Clang outputs.  
  Reqs: LD-U-006. Stories: US-001, US-201.
- [ ] Implement `R_X86_64_GOTPCRELX` and relaxation-aware behavior.  
  Reqs: LD-U-006.
- [ ] Add relocation overflow and sign/zero-extension tests.  
  Reqs: LD-E-006.

### 4.3 i386
- [ ] Complete ABS/PC/GOT/PLT/TLS reloc family support used by GCC/Clang outputs.  
  Reqs: LD-U-006. Stories: US-001.
- [ ] Implement 8/16/32-bit overflow diagnostics and range checks.  
  Reqs: LD-E-006.
- [ ] Add i386 relocation suite (direct + archive + shared).  
  Reqs: LD-U-007.

---

## 5. Section Merge, COMDAT, GC, ICF

### 5.1 Section merge core
- [ ] Merge sections by output policy with alignment/max-flag handling.  
  Reqs: LD-U-008, LD-S-004.
- [ ] Implement COMDAT/group leader selection and duplicate discard.  
  Reqs: LD-U-004, LD-S-004.
- [ ] Implement orphan placement heuristics consistent with GNU defaults.  
  Reqs: LD-S-004.

### 5.2 Garbage collection (`--gc-sections`)
- [ ] Build reachability graph from entry roots + explicit roots + KEEP roots.  
  Reqs: LD-E-004.
- [ ] Implement COMDAT-aware mark/sweep and diagnostics (`--print-gc-sections`).  
  Reqs: LD-E-004, LD-U-010.
- [ ] Add exhaustive GC correctness tests (function/data, ctors/dtors, COMDAT, weak refs).  
  Reqs: LD-U-007.

### 5.3 ICF
- [ ] Implement `--icf=safe` function/data folding.  
  Reqs: LD-O-004, LD-U-007.
- [ ] Implement `--icf=all` extended fold mode.  
  Reqs: LD-O-004.
- [ ] Add guardrails to prevent unsafe merges under reloc/metadata constraints.  
  Reqs: LD-R-003.

---

## 6. Layout and Segment Construction

### 6.1 Address assignment
- [ ] Implement page-aware file/vaddr assignment preserving `offset % page == vaddr % page`.  
  Reqs: LD-U-009, LD-S-004.
- [ ] Implement target defaults for text/data base addresses and PIE behavior.  
  Reqs: LD-U-001, LD-U-009.
- [ ] Add overflow checks for all arithmetic and address-space bounds.  
  Reqs: LD-R-003.

### 6.2 PHDR generation
- [ ] Generate PT_LOAD and standard PHDR set (`PHDR`, `INTERP`, `DYNAMIC`, `TLS`, `NOTE`).  
  Reqs: LD-U-009.
- [ ] Generate PT_GNU_STACK, PT_GNU_RELRO, PT_GNU_EH_FRAME, PT_GNU_PROPERTY where applicable.  
  Reqs: LD-U-009, LD-U-010. Stories: US-401.
- [ ] Ensure W^X-safe mapping policy and `-z text/notext` diagnostics.  
  Reqs: LD-U-009, LD-W-003.

### 6.3 Entry point logic
- [ ] Implement `-e`, `_start`, fallback entry resolution order.  
  Reqs: LD-U-001, LD-E-001.
- [ ] Add tests for entry handling in ET_EXEC/ET_DYN/ET_REL contexts.  
  Reqs: LD-U-007.

---

## 7. Dynamic Linking Artifacts

### 7.1 Core dynamic sections
- [ ] Build `.dynsym/.dynstr` from export policy and resolved dyn symbols.  
  Reqs: LD-S-003.
- [ ] Build `.dynamic` tags and ensure consistency invariants.  
  Reqs: LD-S-003.
- [ ] Implement `--hash-style` (`sysv|gnu|both`) generators.  
  Reqs: LD-U-001, LD-S-003.

### 7.2 GOT/PLT
- [ ] Implement x86-64 GOT/PLT synthesis for lazy and non-lazy paths.  
  Reqs: LD-U-006, LD-S-003.
- [ ] Implement i386 GOT/PLT synthesis for lazy and non-lazy paths.  
  Reqs: LD-U-006, LD-S-003.
- [ ] Wire relocation emission for `.rel[a].plt` and `.rel[a].dyn`.  
  Reqs: LD-U-006.

### 7.3 TLS
- [ ] Implement TLS model handling (GD/LD/IE/LE) for x86-64 and i386.  
  Reqs: LD-U-006, LD-S-003.
- [ ] Generate PT_TLS and TLS dynamic tags correctly.  
  Reqs: LD-U-009.
- [ ] Add TLS integration tests with compiler-emitted sequences.  
  Reqs: LD-U-007.

### 7.4 Symbol versioning
- [ ] Implement `.gnu.version`, `.gnu.version_d`, `.gnu.version_r` generation.  
  Reqs: LD-S-003. Stories: US-201.
- [ ] Add version-script integration tests against GNU behavior.  
  Reqs: LD-U-007.

---

## 8. Linker Script Engine

### 8.1 Parser and AST
- [ ] Implement lexer/parser with source location and include stack context.  
  Reqs: LD-E-007, LD-U-010.
- [ ] Implement AST for `SECTIONS`, `PHDRS`, `MEMORY`, assignments, assertions.  
  Reqs: LD-S-004.

### 8.2 Evaluator
- [ ] Implement expression evaluator with GNU-compatible operator semantics.  
  Reqs: LD-S-004.
- [ ] Implement builtin operators/functions (`ADDR`, `SIZEOF`, `ALIGN`, `LOADADDR`, etc.).  
  Reqs: LD-S-004.
- [ ] Implement `PROVIDE`, `KEEP`, `/DISCARD/`, `SORT_*`, `INSERT` semantics.  
  Reqs: LD-S-004, LD-E-004.

### 8.3 Integration
- [ ] Implement script-driven section placement and PHDR assignment.  
  Reqs: LD-U-009, LD-S-004. Stories: US-202.
- [ ] Add extensive script compatibility corpus tests.  
  Reqs: LD-U-007.

---

## 9. LTO/Plugin Compatibility

- [ ] Implement GNU plugin discovery and handshake flow.  
  Reqs: LD-O-004, LD-U-004.
- [ ] Implement plugin materialization integration with archive extraction/resolution loops.  
  Reqs: LD-S-001.
- [ ] Add GCC/Clang LTO integration tests for C and C++.  
  Reqs: LD-U-007.

---

## 10. Diagnostics and UX Parity

- [ ] Add structured diagnostics with category, source, and remediation hint where possible.  
  Reqs: LD-U-010.
- [ ] Implement map file parity fields (sections, symbols, addresses, object provenance).  
  Reqs: LD-U-011. Stories: US-301.
- [ ] Implement `--reproduce` packaging for bug replay.  
  Reqs: LD-U-007. Stories: US-301.
- [ ] Ensure warnings/errors text compatibility with expected build-system parsers.  
  Reqs: LD-U-010, LD-W-003.

---

## 11. Determinism, Performance, Memory

### 11.1 Determinism
- [ ] Eliminate non-deterministic ordering from all hash/table traversals.  
  Reqs: LD-U-007.
- [ ] Deterministic archive extraction and symbol tie-breakers.  
  Reqs: LD-U-007.
- [ ] Add reproducibility regression tests over repeated links.  
  Reqs: LD-U-007.

### 11.2 Performance
- [ ] Profile and parallelize parse/resolution hot paths (`--threads`).  
  Reqs: Performance NFR.
- [ ] Add arena allocators/caches for symbol/reloc-heavy workloads.  
  Reqs: Performance NFR.
- [ ] Benchmark against lld on representative workloads and record delta.  
  Reqs: Performance NFR.

### 11.3 Memory safety
- [ ] Add hard caps for recursion, expansion, and symbol graph growth.  
  Reqs: LD-R-002.
- [ ] Add fuzz harnesses for ELF/archive/script frontends with sanitizers.  
  Reqs: LD-R-004.

---

## 12. Validation Matrix (Must Pass)

### 12.1 Toolchain integration
- [ ] `cc` + `as` + internal `ld` builds `bin/sh` (native host build path).  
  Reqs: LD-U-001. Stories: US-001.
- [ ] `cc` + `as` + internal `ld` builds GNU bash with no source workarounds.  
  Reqs: LD-U-001, LD-U-005. Stories: US-201.
- [ ] `cc` + `as` + internal `ld` builds coreutils baseline targets.  
  Reqs: LD-U-001, LD-U-005.

### 12.2 Compatibility differential
- [ ] Differential linker result checks against GNU ld/lld for curated corpus.  
  Reqs: LD-U-007.
- [ ] Ensure no backend-forwarding code path remains in `usr.bin/ld`.  
  Reqs: LD-W-001.

### 12.3 Security/robustness
- [ ] 0 crashes on parser fuzz corpus for fixed budget run.  
  Reqs: LD-R-004.
- [ ] All integer-overflow guards covered by targeted tests.  
  Reqs: LD-R-003.

---

## 13. Documentation and Maintenance

- [ ] Update `man/man1/ld.1` to full supported option set and behavior notes.
- [ ] Maintain compatibility notes section documenting intentional divergences from GNU ld/lld.
- [ ] Add internal architecture doc for resolver/layout/reloc backends.
- [ ] Add contributor guide for adding new relocation types and script directives.

---

## 14. Stretch Targets (Post x86-64/i386 parity)

- [ ] Enable AArch64 backend to parity level defined in `SPEC.md`.
- [ ] Enable ARMv7 backend to parity level defined in `SPEC.md`.
- [ ] Add cross-target integration tests in CI.

---

## 15. Completion Gate

Mark this tasklist complete only when all are true:

- [ ] All mandatory requirements (`LD-U-*`, `LD-E-*`, `LD-S-*`, `LD-R-*`) are satisfied.
- [ ] Build/test parity goals for selected benchmark projects are met.
- [ ] No backend forwarding remains.
- [ ] Reproducibility and fuzz gates pass.
- [ ] Temporary linker tasklist removed after migration to steady-state maintenance tickets.
