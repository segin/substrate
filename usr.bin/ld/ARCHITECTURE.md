# Architecture Overview

This document is the living architecture baseline for the Substrate native linker in `usr.bin/ld/`.
It explains how the linker consumes ELF objects and scripts, how it composes the final image through `libelfobj`, and which limits keep archive/script processing bounded.

## 1. Project Structure

```text
usr.bin/ld/
├── ld.h                     # Shared types, and the functions one file has and another uses
├── ld.c                     # The driver: the order of a link, the output file
├── ld_options.c             # The command line: the table of options and its reader
├── ld_util.c                # Vectors, numbers, bytes, diagnostics
├── ld_symtab.c              # Names: the index, sets of them, and the table of the
│                            #   link's global symbols (who defines, who refers)
├── ld_input.c               # Objects, archives, libraries and where they are found
├── ld_dso.c                 # Shared objects as inputs: what they define, symbol versions
├── ld_resolve.c             # What is said about symbols when asked: --trace, --warn-common
├── ld_arch.c                # What differs between i386 and x86-64 where the dynamic
│                            #   sections are made: sizes, relocation numbers, PLT bytes
├── ld_script.c              # Linker scripts: lexer, expressions, parser, application
├── ld_layout.c              # Section order, addresses, segments
├── ld_dynamic.c             # .dynsym, imports, PLT and GOT, .dynamic
├── ld_ehframe.c             # .eh_frame_hdr
├── ld_reloc.c               # Addresses of symbols, the output's own GOT slots, thread-local
│                            #   storage, indirect functions, applying relocations
├── ld_gc.c                  # --gc-sections (of the inputs' sections) and identical code folding
├── ld_map.c                 # The link map and the --reproduce bundle
├── ld_plugin.c              # LTO plugins that are programs
├── ld_buildid.c             # --build-id: the .note.gnu.build-id note, as GNU ld makes it
├── ld_version.c             # Version scripts: --version-script and a script's VERSION { }
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
	-> --icf: which input sections are identical (before the merge)
	-> merge by libelfobj: input sections into output sections, by name
	   or by the script, passing over what was collected and placing one
	   of each class of identical sections
	-> rewriting of thread-local sequences (programs)
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

Description: The options are a table (`options` in `ld_options.c`): each row has an option's spellings, its form (a word alone, a word with a value, a letter with a value joined on) and what sets it -- for most, one member of the context and the value to give it. One reader matches an argument against the table, fetches the value and reports one that is missing, so no option does that for itself. `main` reads the command line, runs the link, and releases what the context owns in one place whichever way it ends. The linker entry point parses CLI policy into a single link context, then loads regular objects, archives, thin archives, DSOs, and script wrappers. A script wrapper (`load_library_script`) names files, found as GNU ld finds them (as named, then on the library path), and `-lNAME`, which goes back through the library search with the naming input's mode, `AS_NEEDED` applied and the nesting bounded. This is the layer that decides target mode, unresolved-symbol behavior, and whether dynamic or relocatable output is being built.

Technologies: C, ELF parsing via `libelfobj`, archive scanning, linker-script front-end logic

Deployment: Installed as `usr/bin/ld` with architecture alias symlinks

### 3.2. Symbol Resolution And Graph Passes

Name: Global symbol state, archive extraction, GC, and ICF

Description: The linker tracks global symbol ownership, resolves weak/strong precedence, and determines when additional archive members must be materialized. Garbage collection (`ld_gc.c`) is decided on the input objects' sections before they are merged: from the entry, what the output exports, what runs unasked and what a script keeps, it follows relocations to the sections they name, and the merge is told which sections to pass over. Identical code folding (`--icf`) is decided on the input sections too, while each function compiled with -ffunction-sections is still a section of its own: sections alike in contents and in what their relocations refer to (the same symbol, or the same place in sections of one class, the classes refined until none splits, so the callers of identical functions are identical too; template instances in COMDAT groups included) are put in classes, and the merge (libelfobj's fold hook) lays out the first of each class and gives the rest its place.  Code is folded in both modes, read-only data under `--icf=all`, nothing writable, and no code whose FDE in `.eh_frame` names an LSDA (what its handlers catch differs while its bytes do not).  `--icf=safe` leaves out, as gold does, a function whose address is taken: one that a relocation of a loaded section (other than `.eh_frame`) refers to in any way but as the target of a `call`, `jmp` or `jcc`, so that pointers to two functions still compare unequal.

Once the inputs are chosen their global symbols are read into one table (`ld_symtab_t`, `ld_symtab.c`): for each name, the definition the link takes (the first strong one, else the first weak), who else defines it, and who first refers to it. Two strong definitions are reported there; the undefined-reference message, the map's "source", and the collector's question "where is this defined" are all answered from it. While inputs are still being chosen the questions are of sets of names (defined so far, still wanted), which are `symset_t` on the same index of names. `libelfobj`'s merge decides what goes into the output by the same rule, in its own code; the tests link each ordering of weak, strong and reference to check that the two agree.

Technologies: C, bounded symbol/object tracking, section reachability analysis, COMDAT handling

Deployment: Internal pass pipeline driven by `run_internal_link()` in `ld.c`

The order of a link is a table, `link_phases` in `ld.c`: one row for each step, with the function that does it, which outputs it is for (`-r`, final, programs only), and what it depends on. The dependencies are a few named points -- the inputs are read, their symbols are in the table, there is an output, the dynamic tables are sized, sections have addresses, relocations are applied, symbol values are addresses -- and each row says which it needs behind it and which must still be ahead. The driver checks that before running a step, so a step moved to where it cannot work stops the link with its name instead of producing a wrong file; and it is the one place the link's inputs, symbol table and output are released.

### 3.3. Script, Layout, And Segment Planning

Name: Linker-script engine and output layout planning

Description: The linker-script path tokenizes and parses scripts, evaluates builtin expressions such as `ADDR` and `SIZEOF`, and drives section ordering and PHDR placement. Default segment planning and script-driven placement converge in the same output layout stage.

An output section's input sections are put together by the merge (`libelfobj`) before anything has an address; the statements between its input section descriptions -- `. = ALIGN(16)`, `. = sym | (1 << 2)` -- are worked out there (`script_rank_gap()`) as far as they can be, and the walk that gives the addresses (`script_assign_addresses()`) checks that the merge left the room it works out. Where it did not, because an expression needed an address the merge could not know, the walk stops the link and `main()` does it over (`link_once()`), carrying the addresses the pass came to (`ld_relayout_t`) and keeping quiet what the pass before already said, until they settle -- as GNU ld sizes its sections over.

An expression's value is an address, and beside it its kind (`lds_val_kind_t`): a plain number, an absolute address, or an address in a section -- GNU ld's "Expression Section" rules, which differ in an output section's body: there `.` is in the section, an absolute symbol is a number, an operator or `ALIGN(exp, n)` works on offsets in the section, and a number assigned is an offset in it. Where a symbol the script sets goes (`lw_home()`) follows from its value's kind, and is noted for the expressions after it (the script's `homes`), since the symbols of the output are made only in the walk's last pass: `setup_size = ALIGN(ABSOLUTE(.), 4096)` is absolute, and `setup_sects = ABSOLUTE(setup_size / 512)` after it reads it as the number it is.

Input sections of different types may make one output section, as in GNU ld: it is of the type of the input first in its order (the lowest rank; `merge_type()` in `libelfobj`), empty or not, except that zeros (NOBITS) first and contents after make PROGBITS, the zeros written out; an array of pointers to call has a word for its entry size by its type alone; and a section in a `PT_NOTE` of the script's `PHDRS` is made a NOTE (`add_script_segments()`'s first pass), as GNU ld makes it -- the vDSO's `.note` begins with the empty PROGBITS `.note.GNU-stack` (`tests/usr.bin/ld/test_script_mixed_types.sh`). Under `-r` the merge follows a script's order of descriptions and its sorts too, as GNU ld does (the rank and sort hooks, without the gap hook: what moves the counter between descriptions is the address walk's, which a relocatable output has not) -- Linux's modules sort their export tables with `SORT(___ksymtab+*)`, which the kernel searches by halving (`tests/usr.bin/ld/test_r_script_order.sh`).

A `PROVIDE` defines its symbol when an input refers to it, or when an expression of a statement the script carries out names it (`script_provide_used()`, repeated until no more are made to, since one `PROVIDE` may be used only by another), as GNU ld takes a name in the script as a reference.

Technologies: C, custom lexer/parser, bounded include stack, address arithmetic with overflow checks

Deployment: Internal to `ld`; no external script interpreter dependency

### 3.4. Relocation And Output Emission

Name: Relocation application, dynamic metadata generation, and final file emission

Description: Before layout the linker plans what the dynamic linker will need (`ld_dynamic.c`, `ld_dso.c`): the imports and their PLT entries and GOT slots, `.dynsym`, `.dynstr`, the hash and version tables, `.dynamic`. They are made after a script's `/DISCARD/` has been applied, so a script that discards `.dynamic` is asked first (`script_output_name()`) and none of them is made, as GNU ld makes none: Linux's x86-64 decompressor is linked `-pie` by such a script, and the tables, as orphans, went in front of the `.head.text` it puts at 0 (`tests/usr.bin/ld/test_script_discard_dynamic.sh`); `libelfobj` makes up a `.dynstr` only for an output that has a `.dynamic`. A symbol is reached through the dynamic linker if it is undefined, or defined in a shared object and preemptible. The output's own GOT slots, a shared object's thread-local entries and the entries and `.iplt` stubs of indirect functions are collected at the same time (`collect_local_got` in `ld_reloc.c`) and sized into `.got` and `.rel[a].dyn` after the imports'. Once addresses are fixed these are filled in and the relocations applied; most are computed by `libelfobj`, and those that need the GOT, the thread-local extent or an instruction rewritten are computed in `apply_all_relocations`. What differs between i386 and x86-64 in this -- the width of a word, whether a record carries its addend, the relocations' numbers, which references are calls, the bytes of the PLT -- is in one description of each machine (`ld_arch_t`, `ld_arch.c`), and the code that collects imports, sizes the tables and fills them in is written once against it. What is still told apart by machine is the rewriting of instructions when relocations are applied (GOT relaxation, the thread-local sequences, the indirect-function stub), where the two machines' instructions have nothing in common to share. The final ELF is written by `libelfobj`. The map and the reproduce bundle are `ld_map.c`.

Technologies: C, `libelfobj` relocation backends, deterministic symbol ordering, map/reproduce emitters

Deployment: Final stage of the in-process linker pipeline

## 4. Data / Persistent Artifacts

### 4.1. Input Link Units

Name: ELF objects, archives, thin archives, DSOs, and linker scripts

Type: ELF binaries and text scripts

Purpose: Provide the link graph, policies, and metadata that shape the final output image.

### 4.2. In-Memory Link State

Name: Link context, resolved symbol tables, section graph, layout plan, dynamic metadata

Type: Process-local C structures declared in `ld.h`: `ld_options_t` is what the command line and the script asked for, and the link does not change it; `ld_ctx_t` holds that as `opt` and, beside it, what the link has found out and decided (the machine, the shared objects, the imports, the GOT's contents); `ld_link_t`, in `ld.c`, is one run of the phases (the inputs read, the symbol table, the output)

Purpose: Hold the evolving link result while symbol resolution, graph passes, layout, and relocation are still running.

### 4.3. Final Outputs

Name: Executables, PIEs, shared objects, relocatable outputs, map files, reproduce bundles

Type: ELF files plus diagnostic/support artifacts

Purpose: Deliver the linked program image and optional debugging/provenance outputs.

`--orphan-handling` is asked before the inputs are merged (`script_check_orphans()` in `ld_script.c`, from `phase_merge`): each input section no description of the script takes -- `script_taker()`, the same matching `script_output_name()` does -- is noted, and under `discard` `script_output_name()` gives it no output section. Under `warn` and `error` (which then fails the link) the noted ones are said in GNU ld's words by `script_say_orphans()`, from `phase_orphans` once the linker's own sections are made (just before addresses are given; under `-r`, which makes none, from `phase_merge`): GNU ld makes its sections in the first input and says them as that input's, so the first input's orphans are said, then each section the linker made (`.interp`, the build ID, the dynamic tables, PLT, GOT and their relocations, `.eh_frame_hdr`) that the script places nowhere, then the other inputs'. Under `discard` the linker's own are left out too (`script_discard_made()`, from `phase_discard_made` once the tables are made and before segments are): the dynamic tables are never made (`plan_dynamic_tables()` finds `.dynamic` discarded), `.rel[a].dyn` and `.rel[a].plt` are removed with their records, and a GOT or PLT the code needs fails the link in GNU ld's words. When a script discards `.dynamic` and not the relocations, GNU ld keeps them: a `.rel[a].dyn` linked to no symbol table, which `libelfobj` writes with its record size all the same and reads back as bytes (`tests/usr.bin/ld/test_orphan_discard_made.sh`). Linux passes `--orphan-handling=warn` to its links; its x86-64 decompressor's dynamic tables went in front of `.head.text` unsaid when they were the linker's (`tests/usr.bin/ld/test_orphan_handling.sh`, `test_orphan_handling_made.sh`).

Where a script's link puts the sections the linker made itself and the script places nowhere is GNU ld's orphan placement (`place_made_orphans()` in `ld_script.c`, the end of `script_order_sections()`, so it holds each time the order is made again): in the order GNU ld makes them (`made_sections[]`), each by its kind -- code, read-only data, dynamic relocations, writable data, zeroed, `.interp` -- after the one before it of that kind, the first after the output section named for the kind, else the last with the kind's protection (one of its own ELF type first), else first. The default order (`reorder_sections_default_policy()`) had the dynamic tables in front of everything, and so in front of the `.head.text` the x86-64 decompressor's `-pie` script puts at 0. A section the script does not place is at the location counter, as one it places is (`lw_pass()`): GNU ld leaves no page between code, read-only and writable orphans, and a script's layout has a `PT_GNU_RELRO` only when the script uses `DATA_SEGMENT_RELRO_END` (`script_has_relro_end()`, `add_default_segments()`) (`tests/usr.bin/ld/test_orphan_place_made.sh`).

A relocatable output has one section symbol for each of its sections that a relocation names, at 0, and none of the inputs': a relocation against an input section's symbol is made against the output section's, the input's place added to the addend, as GNU ld writes it, whether or not the input's symbol has a name (the in-tree assembler names them). Tools that add relocations to a `-r` output take "the" section symbol for their section -- objtool does, for Linux's static call table in `vmlinux.o` -- and with one copied for each input, at its place, they took the last (`tests/usr.bin/ld/test_r_input_section_symbols.sh`).

## 5. External Integrations / APIs

`libelfobj`: The linker relies on `usr.lib/elfobj` for ELF parsing, mutation, relocation application, and final write-out. The script's answer to which output section an input section goes to (`script_output_name()`, the plan's section name hook) is asked once for each input section and kept (`elf_link_input.out_names`): the merge asks it for each relocation, and matching each of Linux's millions against the kernel script's patterns was nine tenths of a link of vmlinux, 25 minutes of each of the build's three (86 seconds now) (`tests/usr.lib/elfobj/test_link_name_hook_once.c`). Under `-r` the inputs' section groups are kept (`elf_link_plan_set_keep_groups()`, unless `--force-group-allocation` or `FORCE_GROUP_ALLOCATION`): `assign_output_groups()` in `elf_link.c` numbers each input group an output group before the merge, a member goes to a section of its group's own, and the writer makes each group's section from its members (`tests/usr.bin/ld/test_r_keep_groups.sh`); otherwise, and in every final link, the members are placed as other sections and the groups are gone (`phase_strip_groups`).

`cc`: The compiler driver shells out to `ld` during full compilation/link workflows and resolves the sibling in-tree linker before PATH fallbacks.

Specifications: Feature and compatibility requirements live in `SPEC.md`; remaining parity work lives in `TASKLIST_LINKER.md`.

Tests: Linker-facing regression surfaces live in `tests/usr.bin/ld/`.

## 6. Build, Installation & Invocation

Host/native mode: `make -C usr.bin/ld NATIVE_BUILD=1` builds a host-runnable linker and forces a matching native `usr.lib/elfobj` build.

Target mode: The default build produces the Substrate-target linker for inclusion in the OS image.

Alias links: The Makefile installs `ld.i386`, `ld.x86_64`, `ld.x86`, and `ld.x64` symlinks for architecture-specific invocation surfaces.

Single-binary design: The linker is one program in eighteen translation units that share one header, `ld.h`, and one link context, `ld_ctx_t`. Sixteen of the files were cut from a single `ld.c` along its function families without changing a function, and `ld_buildid.c` and `ld_version.c` were written after; what is `static` is private to its file, and `ld.h` declares the rest.

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

What is open: `SPEC.md` §0 lists what is specified and absent. `TASKLIST_LINKER.md` is the older backlog and its ticks were not all verified.

Modularity pressure: Every type is still in `ld.h` and every pass still takes the whole `ld_ctx_t`. What was asked for is apart from what the link finds out (`ld_options_t`, the context's `opt`), but a pass can still write either; narrowing what each file can see (its own header, its own part of the context, the options `const`) is the next step, and can be taken a file at a time.

Speed: the link is single-threaded and keeps no cache between links (`SPEC.md` §4.15). Within a link, a shared object is read once and kept (`ld_dso.c`), an archive's members are taken by its symbol index, and `.gnu.hash` has as many buckets as its symbols call for.

## 10. Project Identification

Project Name: Substrate Native Linker

Repository Path: `usr.bin/ld/`

Primary Consumers: Direct user invocation, `usr.bin/cc`, native toolchain/package validation

Date of Last Update: 2026-11-04

## 11. Glossary / Acronyms

DSO: Dynamic Shared Object

GC: Garbage Collection of unreachable sections (`--gc-sections`)

ICF: Identical Code Folding

GOT / PLT: Global Offset Table / Procedure Linkage Table

PHDR: Program Header entry used for runtime segment layout
