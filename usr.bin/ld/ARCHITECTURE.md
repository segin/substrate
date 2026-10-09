# Architecture Overview

This document is the living architecture baseline for the Substrate native linker in `usr.bin/ld/`.
It explains how the linker consumes ELF objects and scripts, how it composes the final image through `libelfobj`, and which limits keep archive/script processing bounded.

## 1. Project Structure

```text
usr.bin/ld/
├── ld.h                     # Shared types, and the functions one file has and another uses
├── ld.c                     # The driver: options, the order of a link, the output file
├── ld_util.c                # Vectors, numbers, bytes, diagnostics
├── ld_symtab.c              # Names: the index, sets of them, and the table of the
│                            #   link's global symbols (who defines, who refers)
├── ld_input.c               # Objects, archives, libraries and where they are found
├── ld_dso.c                 # Shared objects as inputs: what they define, symbol versions
├── ld_resolve.c             # What is said about symbols when asked: --trace, --warn-common
├── ld_script.c              # Linker scripts: lexer, expressions, parser, application
├── ld_layout.c              # Section order, addresses, segments
├── ld_dynamic.c             # .dynsym, imports, PLT and GOT, .dynamic
├── ld_ehframe.c             # .eh_frame_hdr
├── ld_reloc.c               # Addresses of symbols, the output's own GOT slots, thread-local
│                            #   storage, indirect functions, applying relocations
├── ld_gc.c                  # --gc-sections (of the inputs' sections) and identical code folding
├── ld_map.c                 # The link map and the --reproduce bundle
├── ld_plugin.c              # LTO plugins that are programs
├── SPEC.md                  # Feature and parity requirements
├── TASKLIST_LINKER.md       # Remaining compatibility and parity backlog
├── Makefile                 # Build wiring, alias links, libelfobj dependency
├── ARCHITECTURE.md          # This document
├── CONTRIBUTING.md          # Contributor workflow and expectations
└── COMMIT_TEMPLATE.md       # Linker-specific commit hygiene aid

tests/usr.bin/ld/
├── Makefile                 # Runs each script; a failure in one stops the run
├── test_script.sh           # Linker scripts: lexing, expressions, SECTIONS, PHDRS
├── test_archive.sh          # Archives: malformed ones, long names, member selection
├── test_dynamic.sh          # Shared objects: needed names, PLT and GOT, versions,
│                            #   preemption, indirect functions, error messages
├── test_output.sh           # The output file: modes, entry, options, RELRO, PIE,
│                            #   --gc-sections, the map and the --reproduce bundle
├── test_tls.sh              # Thread-local storage, each model, programs and libraries
└── test_got.sh              # GOT slots for the output's own symbols
```

Each script builds the linker for the host out of the tree, links small
freestanding objects with it, and reads the results with the host's
`readelf` and `objdump`.  They show that an output is put together as it
should be; that it runs is checked on a Substrate guest, by hand.

## 2. High-Level System Diagram

```text
ELF Objects / Archives / DSOs / Linker Scripts
	-> input loaders and parsers
	-> global symbol resolution
	-> --gc-sections: which input sections are used (before the merge)
	-> merge by libelfobj: input sections into output sections, by name
	   or by the script, passing over what was collected
	-> rewriting of thread-local sequences (programs), ICF
	-> planning: imports, PLT, GOT slots (imports, then the output's own,
	   then thread-local entries and indirect functions), .dynsym, .dynamic
	-> section order, segments, addresses
	-> filling in what needed addresses, then applying relocations
	-> libelfobj writer
	-> executable / PIE / shared object / relocatable output

cc
	-> ld (resolved from sibling build tree when available)
```

## 3. Core Components

### 3.1. Driver And Input Loading

Name: `ld.c` command parser and `ld_input.c` input collection path

Description: The linker entry point parses CLI policy into a single link context, then loads regular objects, archives, thin archives, DSOs, and script wrappers. This is the layer that decides target mode, unresolved-symbol behavior, and whether dynamic or relocatable output is being built.

Technologies: C, ELF parsing via `libelfobj`, archive scanning, linker-script front-end logic

Deployment: Installed as `usr/bin/ld` with architecture alias symlinks

### 3.2. Symbol Resolution And Graph Passes

Name: Global symbol state, archive extraction, GC, and ICF

Description: The linker tracks global symbol ownership, resolves weak/strong precedence, and determines when additional archive members must be materialized. Garbage collection (`ld_gc.c`) is decided on the input objects' sections before they are merged: from the entry, what the output exports, what runs unasked and what a script keeps, it follows relocations to the sections they name, and the merge is told which sections to pass over. Identical code folding works on the merged output.

Once the inputs are chosen their global symbols are read into one table (`ld_symtab_t`, `ld_symtab.c`): for each name, the definition the link takes (the first strong one, else the first weak), who else defines it, and who first refers to it. Two strong definitions are reported there; the undefined-reference message, the map's "source", and the collector's question "where is this defined" are all answered from it. While inputs are still being chosen the questions are of sets of names (defined so far, still wanted), which are `symset_t` on the same index of names. `libelfobj`'s merge decides what goes into the output by the same rule, in its own code; the tests link each ordering of weak, strong and reference to check that the two agree.

Technologies: C, bounded symbol/object tracking, section reachability analysis, COMDAT handling

Deployment: Internal pass pipeline driven by `run_internal_link()` in `ld.c`

### 3.3. Script, Layout, And Segment Planning

Name: Linker-script engine and output layout planning

Description: The linker-script path tokenizes and parses scripts, evaluates builtin expressions such as `ADDR` and `SIZEOF`, and drives section ordering and PHDR placement. Default segment planning and script-driven placement converge in the same output layout stage.

Technologies: C, custom lexer/parser, bounded include stack, address arithmetic with overflow checks

Deployment: Internal to `ld`; no external script interpreter dependency

### 3.4. Relocation And Output Emission

Name: Relocation application, dynamic metadata generation, and final file emission

Description: Before layout the linker plans what the dynamic linker will need (`ld_dynamic.c`, `ld_dso.c`): the imports and their PLT entries and GOT slots, `.dynsym`, `.dynstr`, the hash and version tables, `.dynamic`. A symbol is reached through the dynamic linker if it is undefined, or defined in a shared object and preemptible. The output's own GOT slots, a shared object's thread-local entries and the entries and `.iplt` stubs of indirect functions are collected at the same time (`collect_local_got` in `ld_reloc.c`) and sized into `.got` and `.rel[a].dyn` after the imports'. Once addresses are fixed these are filled in and the relocations applied; most are computed by `libelfobj`, and those that need the GOT, the thread-local extent or an instruction rewritten are computed in `apply_all_relocations`. The i386 and x86-64 paths are parallel code, not one path over a description of the architecture. The final ELF is written by `libelfobj`. The map and the reproduce bundle are `ld_map.c`.

Technologies: C, `libelfobj` relocation backends, deterministic symbol ordering, map/reproduce emitters

Deployment: Final stage of the in-process linker pipeline

## 4. Data / Persistent Artifacts

### 4.1. Input Link Units

Name: ELF objects, archives, thin archives, DSOs, and linker scripts

Type: ELF binaries and text scripts

Purpose: Provide the link graph, policies, and metadata that shape the final output image.

### 4.2. In-Memory Link State

Name: Link context, resolved symbol tables, section graph, layout plan, dynamic metadata

Type: Process-local C structures declared in `ld.h`

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

Single-binary design: The linker is one program in fourteen translation units that share one header, `ld.h`, and one link context, `ld_ctx_t`. The files were cut from a single `ld.c` along its function families without changing a function; what is `static` is private to its file, and `ld.h` declares the rest.

## 7. Security Considerations

Input ceilings: Hard limits bound tracked symbols, tracked input objects, archive scan passes, linker-script include depth, and individual script token length.

Checked arithmetic: Virtual-address, offset, and table-size calculations use explicit checked add/multiply helpers to reject overflow instead of wrapping.

Relocation diagnostics: Relocation failures include section, symbol, and relocation context so malformed input is easier to diagnose without silent corruption.

Determinism: Archive scanning, symbol ordering, and reproduce/map outputs are designed to support repeatable builds and repeated-link regression checks.

## 8. Development & Testing Environment

Local build: `make -C usr.bin/ld NATIVE_BUILD=1`

Primary regression surface: `tests/usr.bin/ld/`, six shell scripts run by `make -C tests/usr.bin/ld` and by the `host-tests` job of the `ci` workflow. What each covers is listed in section 1.

What they do not cover: that the outputs run. That is checked by linking programs with a host build of the linker and running them on Substrate guests of both architectures.

Integration role: The linker is exercised directly by its own suite and indirectly through compiler-driven and external package builds.

## 9. Future Considerations / Roadmap

What is open: `docs/ld-audit.md` is the checked record of what the linker does and does not do, with evidence; `SPEC.md` §0 lists what is specified and absent. `TASKLIST_LINKER.md` is the older backlog and its ticks were not all verified.

Modularity pressure: The split is by file only. Every type is still in `ld.h` and every pass still takes the whole `ld_ctx_t`; narrowing what each file can see (its own header, its own part of the context) is the next step, and can be taken a file at a time.

Structure still to come: a description table for the two architectures in place of the parallel code; an option table and a phase list in place of the chain of comparisons and the long `run_internal_link`.

Speed: shared objects are re-read for each question asked of them, archives are searched by reading every member, and `.gnu.hash` is written with one bucket.

## 10. Project Identification

Project Name: Substrate Native Linker

Repository Path: `usr.bin/ld/`

Primary Consumers: Direct user invocation, `usr.bin/cc`, native toolchain/package validation

Date of Last Update: 2026-10-09

## 11. Glossary / Acronyms

DSO: Dynamic Shared Object

GC: Garbage Collection of unreachable sections (`--gc-sections`)

ICF: Identical Code Folding

GOT / PLT: Global Offset Table / Procedure Linkage Table

PHDR: Program Header entry used for runtime segment layout
