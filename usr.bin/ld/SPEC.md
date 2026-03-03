# `usr.bin/ld` Full ELF Linker Specification

Version: 2.0  
Status: Draft for implementation  
Primary target: x86-64 SysV ELF  
Secondary target: i386 SysV ELF  
Stretch targets: AArch64, ARMv7 ELF

---

## 1. Objective

Implement `usr.bin/ld` as a complete standalone ELF linker that replaces practical usage of:

- GNU gold (`ld.gold`)
- LLVM lld (ELF mode, `ld.lld`)

for Substrate toolchain workflows, including kernel/userland builds, large C/C++ projects, and GNU-style build systems.

This linker must not depend on host linker binaries for output generation.

---

## 2. Scope and Compatibility Target

### 2.1 In Scope

- Full static, relocatable, PIE, shared-object linking flows.
- GNU `ld`-compatible CLI behavior for common and advanced flags.
- ELF object/archive/shared input ingestion.
- Linker script support at practical parity.
- Symbol resolution, relocation, section/segment layout, dynamic metadata generation.
- TLS, GOT/PLT, IFUNC, symbol versioning.
- Diagnostics, map files, reproducible outputs, deterministic builds.
- High-scale performance and memory behavior.

### 2.2 Out of Scope (Initial)

- Non-ELF formats (`a.out`, COFF, Mach-O).
- Non-SysV ABIs.
- Full mold-specific extensions (can be added later).

### 2.3 Compatibility Baseline

Behavioral compatibility target for accepted output and option semantics:

- GNU binutils `ld` 2.4x common behavior
- gold 1.16+ behavior where it differs from BFD ld
- lld ELF 17+ behavior where it differs from GNU ld

When behavior differs across tools, compatibility precedence is:

1. GNU `ld`/gold behavior required by Linux/Unix build systems.
2. lld-compatible behavior for modern build stacks.
3. Explicit Substrate policy only when (1) and (2) are ambiguous.

---

## 3. Architecture

### 3.1 Linker Pipeline

1. CLI/options parse and normalization.
2. Input graph expansion (`.o`, `.a`, `.so`, scripts, groups).
3. Symbol table build and resolution.
4. Archive member extraction fixpoint resolution.
5. Section merge, COMDAT/group resolution, GC/ICF.
6. Address assignment and output section mapping.
7. Relocation planning and application.
8. Dynamic artifact synthesis (`.dynsym`, `.dynamic`, `.got`, `.plt`, hashes, versions).
9. PHDR generation and segment layout.
10. Final validation and deterministic write.

### 3.2 Components

- `driver`: CLI parser, option semantics, target mode.
- `input`: ELF/archive/script loaders.
- `resolver`: symbol resolution and archive extraction engine.
- `layout`: section/segment planner.
- `reloc`: target relocation backends.
- `dyn`: dynamic linking metadata, GOT/PLT/TLS builders.
- `script`: linker script parser + evaluator.
- `emit`: final ELF writer.
- `diag`: diagnostics, notes, map and trace output.
- `perf`: threading/cache/memory optimizations.

---

## 4. Feature Parity Matrix (Required)

This section defines every major capability area needed to replace `lld`/`gold` in real builds.

### 4.1 Core Driver/CLI

- `-o`, `-m`, `-L`, `-l`, `-r`, `-shared`, `-pie`, `-static`, `-Bstatic`, `-Bdynamic`
- `--start-group/--end-group`, `--whole-archive/--no-whole-archive`
- `-e`, `--entry`, `--defsym`, `--undefined`
- `--gc-sections`, `--print-gc-sections`
- `--icf=safe|all|none`
- `--build-id=*`
- `--hash-style=sysv|gnu|both`
- `-Map`
- `--as-needed/--no-as-needed`
- `-soname`, `-rpath`, `-rpath-link`
- `--dynamic-linker`
- `-z` family (`relro`, `now`, `execstack`, `noexecstack`, `text`, `notext`, etc.)
- `--emit-relocs`
- `--strip-all`, `--strip-debug`, `-s`, `-S`
- `--fatal-warnings`, `--warn-common`, unresolved symbol policy flags
- `--reproduce`
- `-v`, `--version`, `--trace`, `--trace-symbol`

### 4.2 Input Object Handling

- ET_REL parsing (ELF32+ELF64).
- `.symtab/.strtab`, `.dynsym/.dynstr`.
- REL and RELA section processing.
- COMDAT/section groups.
- `.eh_frame` / `.gcc_except_table` pass-through and merge logic.
- notes/properties (`.note.gnu.property`, build notes).

### 4.3 Archive Handling

- SysV and BSD archive variants.
- thin archives.
- symbol-index-driven lazy extraction.
- group iterative extraction fixpoint (`--start-group`).
- deterministic member processing.

### 4.4 Shared Object Inputs

- DSO symbol import model.
- DT_NEEDED emission with `--as-needed`.
- versioned symbol lookup from DSOs.
- copy relocation and IFUNC handling paths.

### 4.5 Linker Scripts

- `SECTIONS`, `PHDRS`, `MEMORY`.
- `ENTRY`, `OUTPUT`, `OUTPUT_FORMAT`, `OUTPUT_ARCH`, `SEARCH_DIR`.
- `INPUT`, `GROUP`, `INCLUDE`, `STARTUP`.
- `/DISCARD/`, `KEEP`, `SORT_*`.
- `PROVIDE`, `PROVIDE_HIDDEN`, assignments.
- expression engine (`ADDR`, `SIZEOF`, `LOADADDR`, `ALIGN`, arithmetic).
- `ASSERT`.

### 4.6 Symbol Resolution

- strong/weak/common precedence.
- multiple definition diagnostics.
- common symbol allocation.
- visibility handling.
- protected/hidden semantics.
- export policy controls (`--export-dynamic*`, dynamic lists).
- symbol version definitions/references.

### 4.7 Relocations

- full i386 and x86-64 relocation sets required by modern C/C++ toolchains:
  - absolute, PC-relative, size relocations
  - GOT/PLT relocations
  - TLS model relocations (GD/LD/IE/LE)
  - IFUNC/IRELATIVE
- overflow checks with precise location diagnostics.
- range-extension thunk/veneer generation where required.

### 4.8 Section GC and ICF

- reachability from root symbols and `KEEP`.
- COMDAT-aware GC.
- optional ICF safe/all modes.
- report mode for discarded entities.

### 4.9 Dynamic Runtime Sections

- `.dynamic`, `.dynsym`, `.dynstr`
- `.hash` and `.gnu.hash`
- `.gnu.version*`
- `.got`, `.got.plt`, `.plt`, `.plt.got`, `.plt.sec` where applicable
- relocation sections (`.rel[a].dyn`, `.rel[a].plt`)
- RELRO boundaries and PT_GNU_RELRO

### 4.10 Segments and Program Headers

- PT_LOAD mapping with ABI-correct alignment.
- PT_PHDR, PT_INTERP, PT_DYNAMIC, PT_TLS, PT_NOTE.
- PT_GNU_STACK, PT_GNU_RELRO, PT_GNU_EH_FRAME, PT_GNU_PROPERTY.
- entrypoint resolution.

### 4.11 Debug/Unwind Fidelity

- preserve DWARF sections unless stripped.
- `.eh_frame` merge validity.
- optional `.eh_frame_hdr` generation.

### 4.12 LTO / Plugin Compatibility

- GNU plugin protocol compatibility for GCC LTO flows.
- fallback behavior when plugin unavailable.
- symbol resolution and archive extraction with plugin materialization.

### 4.13 Diagnostics and Tooling

- file:line:col diagnostics where script/input context exists.
- include stack / script expansion context.
- unresolved reference call chain context.
- map file with section/symbol provenance.

### 4.14 Determinism and Reproducibility

- deterministic ordering independent of host hash seed.
- stable section/symbol ordering.
- reproducible archive extraction order.
- reproducible timestamps control.

### 4.15 Performance and Scale

- parallel input parse/resolution.
- incremental internal caches.
- bounded memory growth for large links.
- target benchmarks against large C++ codebases.

---

## 5. INCOSE/EARS Requirements

Requirement IDs are normative.

### 5.1 Ubiquitous (U)

- **LD-U-001**: The linker **shall** generate valid ELF outputs for ET_REL, ET_EXEC, and ET_DYN.
- **LD-U-002**: The linker **shall** support x86-64 SysV ELF as a first-class target.
- **LD-U-003**: The linker **shall** support i386 SysV ELF as a compatibility target.
- **LD-U-004**: The linker **shall** support static archive and relocatable object inputs in one link graph.
- **LD-U-005**: The linker **shall** resolve symbols with GNU-compatible precedence rules.
- **LD-U-006**: The linker **shall** apply supported relocations with overflow detection.
- **LD-U-007**: The linker **shall** emit deterministic outputs from identical inputs/options.
- **LD-U-008**: The linker **shall** honor section alignment constraints.
- **LD-U-009**: The linker **shall** generate ABI-correct PT_LOAD and dynamic PHDRs.
- **LD-U-010**: The linker **shall** provide diagnostics in `tool: file:line:col: level: message` form when source location is known.
- **LD-U-011**: The linker **shall** provide a map file when `-Map` is requested.
- **LD-U-012**: The linker **shall** reject malformed ELF inputs safely.

### 5.2 Event-Driven (E)

- **LD-E-001**: **When** an undefined non-weak symbol remains in an ET_EXEC link, **the linker shall** fail with symbol and reference context.
- **LD-E-002**: **When** `--allow-undefined` is enabled, **the linker shall** continue and mark unresolved references per output type policy.
- **LD-E-003**: **When** duplicate strong definitions are discovered, **the linker shall** emit an error unless override policy is enabled.
- **LD-E-004**: **When** `--gc-sections` is enabled, **the linker shall** discard unreachable sections except `KEEP` roots.
- **LD-E-005**: **When** `--as-needed` is enabled, **the linker shall** omit unused DSO DT_NEEDED entries.
- **LD-E-006**: **When** a relocation overflows destination width, **the linker shall** fail with relocation type, symbol, and location.
- **LD-E-007**: **When** script parsing fails, **the linker shall** report exact script location and abort link.

### 5.3 State-Driven (S)

- **LD-S-001**: **While** resolving archives in group mode, **the linker shall** iterate extraction until no new undefineds are resolved.
- **LD-S-002**: **While** building ET_DYN/PIE outputs, **the linker shall** maintain position-independent relocation semantics.
- **LD-S-003**: **While** generating dynamic artifacts, **the linker shall** keep `.dynsym`, hash tables, and version tables mutually consistent.
- **LD-S-004**: **While** computing layout, **the linker shall** preserve required relative ordering constraints from scripts and ABI defaults.

### 5.4 Optional (O)

- **LD-O-001**: The linker **may** support AArch64 output once x86-64/i386 parity is complete.
- **LD-O-002**: The linker **may** support ARMv7 output once x86-64/i386 parity is complete.
- **LD-O-003**: The linker **may** provide incremental linking acceleration after baseline parity.
- **LD-O-004**: The linker **may** support additional output tuning compatible with mold/lld extensions.

### 5.5 Unwanted Behavior (W)

- **LD-W-001**: **If** host linker binaries are unavailable, **the linker shall not** require them to produce outputs.
- **LD-W-002**: **If** input metadata is malformed, **the linker shall not** read/write out of bounds.
- **LD-W-003**: **If** warnings are configured as fatal, **the linker shall not** emit a successful output file.
- **LD-W-004**: **If** deterministic mode is selected, **the linker shall not** include unstable host timestamps.

### 5.6 Security and Robustness (R)

- **LD-R-001**: The linker **shall** bounds-check all file offsets and sizes before access.
- **LD-R-002**: The linker **shall** cap recursion and graph expansion to configured limits.
- **LD-R-003**: The linker **shall** fail closed on integer overflow in address/size arithmetic.
- **LD-R-004**: The linker **shall** fuzz-test archive/script/ELF parser entrypoints.

---

## 6. User Stories

### 6.1 Toolchain Engineer

- **US-001**: As a toolchain engineer, I want `cc` to call our `ld` directly so we can build host binaries without host linker dependencies.
- **US-002**: As a toolchain engineer, I want `-L/-l` and archive group semantics to match GNU behavior so large legacy projects link unchanged.
- **US-003**: As a toolchain engineer, I want deterministic outputs so CI binary diffs are stable.

### 6.2 Kernel Developer

- **US-101**: As a kernel developer, I want static ET_EXEC links with precise section placement so kernel boot images are reproducible.
- **US-102**: As a kernel developer, I want actionable unresolved symbol diagnostics with object/member provenance.

### 6.3 Userland Porting Engineer

- **US-201**: As a porter, I want shared-library linking with versioned symbols so glibc/coreutils/bash-style builds succeed.
- **US-202**: As a porter, I want linker script support compatible with GNU projects and autogenerated scripts.

### 6.4 Build/Release Engineer

- **US-301**: As a release engineer, I want `--reproduce` and stable map outputs for bug reproduction.
- **US-302**: As a release engineer, I want `--build-id` support for symbol server and crash triage.

### 6.5 Security Engineer

- **US-401**: As a security engineer, I want `-z relro`, `-z now`, `-z noexecstack`, and W^X-safe segment layout for hardened binaries.

---

## 7. Non-Functional Requirements

- **Correctness**: byte-level ABI correctness for produced ELF metadata and relocations.
- **Performance**: link-time within 1.25x of lld for target workloads by end-state.
- **Scalability**: handle 100k+ symbols and multi-GB input graphs without pathological blowups.
- **Determinism**: reproducible output with fixed inputs and options.
- **Maintainability**: architecture backend interfaces with strict unit tests and traceability.

---

## 8. Verification Strategy

### 8.1 Compliance Test Layers

1. Unit tests per subsystem.
2. Golden-output linker tests vs known binaries.
3. Differential tests vs GNU ld/lld on corpus.
4. Integration builds:
   - shell (`bin/sh`)
   - bash
   - coreutils
   - kernel image links
5. Fuzzing:
   - ELF parser
   - archive parser
   - script parser

### 8.2 Exit Criteria for “lld/gold Replacement”

- 0 critical mismatches on supported option matrix.
- 0 crashes on fuzz corpus under sanitizers.
- Build and test parity for selected benchmark projects.
- No backend-forwarding paths remaining.

---

## 9. Traceability

- Tasklist items in `TASKLIST_LINKER.md` must reference:
  - Requirement IDs (`LD-U-*`, `LD-E-*`, `LD-S-*`, `LD-R-*`)
  - User stories (`US-*`)
- No checkbox may be marked complete without:
  - tests
  - documentation updates
  - requirement linkage

---

## 10. Deliverables

- Standalone `usr.bin/ld/ld` binary.
- Updated manual page (`man/man1/ld.1`).
- Regression/fuzz/perf test suites.
- Compatibility report against GNU ld/lld/gold behaviors.
- Removed host-linker forwarding paths.

