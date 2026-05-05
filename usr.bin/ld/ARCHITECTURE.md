# Architecture Overview

This document is the living architecture baseline for the Substrate native linker in `usr.bin/ld/`.
It explains how the linker consumes ELF objects and scripts, how it composes the final image through `libelfobj`, and which limits keep archive/script processing bounded.

## 1. Project Structure

```text
usr.bin/ld/
├── ld.c                     # CLI parsing, input loading, symbol resolution, layout, emit
├── SPEC.md                  # Feature and parity requirements
├── TASKLIST_LINKER.md       # Remaining compatibility and parity backlog
├── Makefile                 # Build wiring, alias links, libelfobj dependency
├── ARCHITECTURE.md          # This document
├── CONTRIBUTING.md          # Contributor workflow and expectations
└── COMMIT_TEMPLATE.md       # Linker-specific commit hygiene aid

tests/usr.bin/ld/
├── README.md                # Test taxonomy and requirement mapping
├── run_all.sh               # Dashboard-style execution wrapper
├── test_*.sh                # Feature, hardening, runtime, and compatibility tests
└── corpus/                  # Linker-script and malformed-input corpora
```

## 2. High-Level System Diagram

```text
ELF Objects / Archives / DSOs / Linker Scripts
	-> input loaders and parsers
	-> global symbol resolution
	-> section policy (merge, GC, ICF, script placement)
	-> segment and virtual address layout
	-> relocation + dynamic metadata generation
	-> libelfobj writer
	-> executable / PIE / shared object / relocatable output

cc
	-> ld (resolved from sibling build tree when available)
```

## 3. Core Components

### 3.1. Driver And Input Loading

Name: `ld.c` command parser and input collection path

Description: The linker entry point parses CLI policy into a single link context, then loads regular objects, archives, thin archives, DSOs, and script wrappers. This is the layer that decides target mode, unresolved-symbol behavior, and whether dynamic or relocatable output is being built.

Technologies: C, ELF parsing via `libelfobj`, archive scanning, linker-script front-end logic

Deployment: Installed as `usr/bin/ld` with architecture alias symlinks

### 3.2. Symbol Resolution And Graph Passes

Name: Global symbol state, archive extraction, GC, and ICF

Description: The linker tracks global symbol ownership, resolves weak/strong precedence, determines when additional archive members must be materialized, and applies section-graph passes such as garbage collection and identical code folding.

Technologies: C, bounded symbol/object tracking, section reachability analysis, COMDAT handling

Deployment: Internal pass pipeline inside `ld.c`

### 3.3. Script, Layout, And Segment Planning

Name: Linker-script engine and output layout planning

Description: The linker-script path tokenizes and parses scripts, evaluates builtin expressions such as `ADDR` and `SIZEOF`, and drives section ordering and PHDR placement. Default segment planning and script-driven placement converge in the same output layout stage.

Technologies: C, custom lexer/parser, bounded include stack, address arithmetic with overflow checks

Deployment: Internal to `ld`; no external script interpreter dependency

### 3.4. Relocation And Output Emission

Name: Relocation application, dynamic metadata generation, and final file emission

Description: Once layout is fixed, the linker applies architecture-specific relocation backends, builds dynamic sections such as `.dynsym`, `.dynstr`, `.dynamic`, hash/version tables, GOT/PLT/TLS artifacts, and emits the final ELF through `libelfobj`. Optional map and reproduce outputs also live here.

Technologies: C, `libelfobj` relocation backends, deterministic symbol ordering, map/reproduce emitters

Deployment: Final stage of the in-process linker pipeline

## 4. Data / Persistent Artifacts

### 4.1. Input Link Units

Name: ELF objects, archives, thin archives, DSOs, and linker scripts

Type: ELF binaries and text scripts

Purpose: Provide the link graph, policies, and metadata that shape the final output image.

### 4.2. In-Memory Link State

Name: Link context, resolved symbol tables, section graph, layout plan, dynamic metadata

Type: Process-local C structures inside `ld.c`

Purpose: Hold the evolving link result while symbol resolution, graph passes, layout, and relocation are still running.

### 4.3. Final Outputs

Name: Executables, PIEs, shared objects, relocatable outputs, map files, reproduce bundles

Type: ELF files plus diagnostic/support artifacts

Purpose: Deliver the linked program image and optional debugging/provenance outputs.

## 5. External Integrations / APIs

`libelfobj`: The linker relies on `usr.lib/elfobj` for ELF parsing, mutation, relocation application, and final write-out.

`cc`: The compiler driver shells out to `ld` during full compilation/link workflows and resolves the sibling in-tree linker before PATH fallbacks.

Specifications: Feature and compatibility requirements live in `SPEC.md`; remaining parity work lives in `TASKLIST_LINKER.md`.

Tests: Linker-facing regression surfaces live in `tests/usr.bin/ld/`.

## 6. Build, Installation & Invocation

Host/native mode: `make -C usr.bin/ld NATIVE_BUILD=1` builds a host-runnable linker and forces a matching native `usr.lib/elfobj` build.

Target mode: The default build produces the Substrate-target linker for inclusion in the OS image.

Alias links: The Makefile installs `ld.i386`, `ld.x86_64`, `ld.x86`, and `ld.x64` symlinks for architecture-specific invocation surfaces.

Single-binary design: The linker currently lives in one large translation unit (`ld.c`), so architectural boundaries are enforced by function families and internal state partitions rather than separate compilation units.

## 7. Security Considerations

Input ceilings: Hard limits bound tracked symbols, tracked input objects, archive scan passes, linker-script include depth, and individual script token length.

Checked arithmetic: Virtual-address, offset, and table-size calculations use explicit checked add/multiply helpers to reject overflow instead of wrapping.

Relocation diagnostics: Relocation failures include section, symbol, and relocation context so malformed input is easier to diagnose without silent corruption.

Determinism: Archive scanning, symbol ordering, and reproduce/map outputs are designed to support repeatable builds and repeated-link regression checks.

## 8. Development & Testing Environment

Local build: `make -C usr.bin/ld NATIVE_BUILD=1`

Primary regression surface: `tests/usr.bin/ld/` covers archive parsing, unresolved-symbol policies, dynamic tags, relocations, script frontends, GC/ICF, host dual-arch behavior, hardening inputs, and deterministic reproduce flows.

Test orchestration: `tests/usr.bin/ld/run_all.sh` provides a dashboard-style wrapper over the granular shell tests.

Integration role: The linker is exercised directly by its own suite and indirectly through compiler-driven and external package builds.

## 9. Future Considerations / Roadmap

Parity backlog: `TASKLIST_LINKER.md` continues to track GNU-compatible features and remaining edge-case work.

Modularity pressure: The current single-file design keeps state centralized, but future growth may warrant extraction of script, relocation, or loader subsystems into dedicated translation units without changing the pipeline contract.

Dynamic-link coverage: Continued work centers on deeper relocation models, versioning edge cases, and script compatibility breadth.

## 10. Project Identification

Project Name: Substrate Native Linker

Repository Path: `usr.bin/ld/`

Primary Consumers: Direct user invocation, `usr.bin/cc`, native toolchain/package validation

Date of Last Update: 2026-04-22

## 11. Glossary / Acronyms

DSO: Dynamic Shared Object

GC: Garbage Collection of unreachable sections (`--gc-sections`)

ICF: Identical Code Folding

GOT / PLT: Global Offset Table / Procedure Linkage Table

PHDR: Program Header entry used for runtime segment layout
