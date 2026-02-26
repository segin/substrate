# `usr.bin/ld` Comprehensive Tasklist

Purpose: long-term actionable checklist for a production ELF linker for i386 and x86_64 with deterministic output and strong ABI correctness.

Scope:
- CLI/driver behavior, symbol resolution, relocations, layout, dynamic linking, diagnostics.
- Compatibility with `as`, `cc`, loader tooling, and `libelfobj`.

Execution policy:
- Complete one checkbox at a time.
- Add tests before marking done.
- Validate i386 and x86_64 paths explicitly.
- Preserve deterministic output and failure behavior.

---

## 1) Core CLI and Mode Selection
- [ ] Baseline executable link path (`ld objs... -o a.out`).
- [ ] `-m elf_i386` and `-m elf_x86_64` mode selection.
- [ ] Mode inference from first input object.
- [ ] `-r` relocatable link mode.
- [ ] `-shared` ET_DYN output mode.
- [ ] `-pie` output semantics.
- [ ] `-o` output target semantics.
- [ ] `-L` library search path handling.
- [ ] `-lfoo` library resolution order behavior.
- [ ] Stable CLI diagnostics and option conflict handling.

## 2) Input Object and Archive Handling
- [ ] Parse ELF32/ELF64 ET_REL inputs.
- [ ] Accept and validate static archives (`.a`).
- [ ] Archive member extraction by unresolved symbol demand.
- [ ] Support repeated archive scan semantics (`--start-group/--end-group`).
- [ ] Reject class/machine mismatches with clear diagnostics.
- [ ] Handle malformed/truncated input safely.
- [ ] Preserve input order semantics where required.
- [ ] Support linker script `INPUT()`/`GROUP()` primitives.
- [ ] Reject unsupported input kinds deterministically.
- [ ] Track per-input provenance for diagnostic reporting.

## 3) Symbol Resolution and Visibility
- [ ] Build global symbol table across all inputs.
- [ ] Binding rules: local/global/weak/common handling.
- [ ] Multiple-definition diagnostics.
- [ ] Undefined symbol diagnostics and policy controls.
- [ ] Common symbol allocation and alignment semantics.
- [ ] Hidden/protected/default visibility semantics.
- [ ] Symbol version definition/reference plumbing.
- [ ] Interposition behavior for shared object linking.
- [ ] Archive extraction interactions with weak symbols.
- [ ] Deterministic symbol selection in tie cases.

## 4) Relocation Processing
- [ ] i386 relocation set required by produced compiler output.
- [ ] x86_64 relocation set required by produced compiler output.
- [ ] REL and RELA relocation application support.
- [ ] PC-relative relocation handling.
- [ ] GOT/PLT relocation handling.
- [ ] TLS relocation handling (static and dynamic models).
- [ ] Relocation overflow detection with precise diagnostics.
- [ ] Unsupported relocation reporting with relocation/type context.
- [ ] Relocation against discarded/GC sections handling.
- [ ] Architecture backend abstraction for relocation rules.

## 5) Section Merging and Layout
- [ ] Merge input sections by name/type/flags policy.
- [ ] Alignment propagation and padding correctness.
- [ ] Output section ordering rules.
- [ ] SHF_MERGE/SHF_STRINGS handling.
- [ ] COMDAT/group resolution semantics.
- [ ] Orphan section placement policy.
- [ ] `.bss` NOBITS accounting correctness.
- [ ] Section GC roots and retention policy hooks.
- [ ] Optional identical code folding hooks.
- [ ] Deterministic final section offset assignment.

## 6) Segment Construction and Program Headers
- [ ] Build PT_LOAD segments with correct permissions.
- [ ] Build PT_DYNAMIC for dynamic outputs.
- [ ] Build PT_INTERP when required.
- [ ] Build PT_TLS when TLS present.
- [ ] Align segment/file offsets to ABI expectations.
- [ ] Entry point selection and validation.
- [ ] `-z` policy options plumbing (`relro`, `now`, etc.).
- [ ] W^X-oriented segment policy validation.
- [ ] PHDR ordering consistency.
- [ ] Loader-compatibility verification against system tools.

## 7) Dynamic Linking Artifacts
- [ ] `.dynamic` tag population correctness.
- [ ] `.dynsym` / `.dynstr` construction.
- [ ] PLT and GOT section synthesis.
- [ ] `DT_NEEDED` emission from link inputs/options.
- [ ] RUNPATH/RPATH option handling.
- [ ] Hash tables: SYSV hash and GNU hash generation.
- [ ] Symbol version sections (`.gnu.version*`) generation.
- [ ] Copy relocations and IFUNC policy handling.
- [ ] Lazy/eager binding mode controls.
- [ ] Shared object SONAME handling.

## 8) Linker Scripts and Control Language
- [ ] Basic script parser for `SECTIONS`, `MEMORY`, `PHDRS`.
- [ ] Section placement expressions and symbol assignments.
- [ ] Built-in linker symbols (`_start`, `_end`, etc.) handling.
- [ ] Script conditionals and includes where supported.
- [ ] Error diagnostics for malformed scripts.
- [ ] Command-line script override semantics (`-T`).
- [ ] Default script selection by mode/target.
- [ ] Deterministic expression evaluation and overflow checks.
- [ ] Compatibility subset documentation vs GNU ld.
- [ ] Script-driven map output consistency.

## 9) Incremental and Advanced Linking Hooks
- [ ] Incremental relink metadata design and placeholders.
- [ ] Partial link (`-r`) relocation preservation correctness.
- [ ] LTO placeholder/object passthrough behavior.
- [ ] Section GC (`--gc-sections`) implementation.
- [ ] Retain policies (`KEEP`) in scripts.
- [ ] Symbol-ordering hooks for profile-guided layout.
- [ ] Thin archive and plugin interaction hooks.
- [ ] Future parallel link planning hooks.
- [ ] Optional relaxation framework hooks.
- [ ] Compatibility strategy for non-implemented advanced flags.

## 10) Diagnostics, Safety, and Reproducibility
- [ ] High-quality diagnostics with input/member context.
- [ ] Map file output (`-Map`) correctness.
- [ ] Structured errors for script, symbol, relocation phases.
- [ ] Hard bounds checks for all offsets/sizes from inputs.
- [ ] Deterministic output for identical inputs/options.
- [ ] Stable ordering of diagnostics.
- [ ] OOM-safe failure behavior.
- [ ] Validation pass before final write.
- [ ] Fuzz-hardening for object/script parsers.
- [ ] Security-oriented checks for malformed ELF edge cases.

## 11) Testing, Fuzzing, and Benchmarks
- [ ] Unit tests: symbol resolution edge cases.
- [ ] Unit tests: relocation apply/overflow edge cases.
- [ ] Unit tests: section/segment layout constraints.
- [ ] Integration tests: `cc` + `as` + `ld` hello-world.
- [ ] Integration tests: shared lib + executable dynamic load path.
- [ ] Integration tests: static archive extraction scenarios.
- [ ] Differential tests vs GNU ld for selected corpora.
- [ ] `readelf`/`objdump` structural validation tests.
- [ ] Fuzzing harnesses for ELF and script inputs.
- [ ] Large-link benchmarks (many objects/symbols/relocs).

## 12) Integration and Rollout
- [ ] Recursive Makefile integration and install targets.
- [ ] Host build path verification.
- [ ] Substrate target build path verification.
- [ ] Driver integration contract with `cc`.
- [ ] Man page and SPEC synchronization.
- [ ] Migration checklist from delegated host-linker flow.
- [ ] ABI compatibility gates in CI.
- [ ] Reproducible-build gate in CI.
- [ ] Release criteria and rollout phases documented.
- [ ] Post-release defect triage checklist.

