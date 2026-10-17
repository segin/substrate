# Architecture Overview

This document is the living architecture baseline for the Substrate native C compiler in `usr.bin/cc/`.
It explains how the compiler is partitioned into driver, frontend, middle-end, and backend stages, and how those stages interact with the in-tree assembler and linker.

## 1. Project Structure

```text
usr.bin/cc/
├── cmd/
│   ├── cc.c                 # Driver, tool resolution, stage orchestration, fork/exec glue
│   └── pipeline.c           # C -> IR -> assembly coordination
├── common/
│   ├── asm_constraint.c     # Inline-asm constraint classification
│   ├── diag.c               # Diagnostics: record the first error, print it with its source line
│   ├── lang.c               # C language modes: the one decoder of -std=
│   ├── options.c            # The settings of one compilation (cc_options_t)
│   ├── target.c             # Per-target data layout (psABI sizes and alignments)
│   └── util.c               # String duplication and string vectors
├── frontend/
│   ├── preproc.c            # Preprocessor helpers and expansion bounds
│   ├── lexer.c              # C tokenization
│   ├── parser.c             # Recursive-descent parser
│   ├── sema.c               # Type checking and semantic validation
│   ├── builtin.c            # Builtin-function and intrinsic handling
│   ├── consteval.c          # Integer constant expressions: the one evaluator
│   └── types.c              # Scalar type sizes and alignments, for every phase
├── middle/
│   ├── ast2ir.c             # AST -> SSA lowering
│   ├── irbuild.c            # Appending IR instructions (cc_irbuild.h), for lowering and the ABI code
│   ├── abi_x86_64.c         # x86-64 SysV: how aggregates are passed and returned; va_arg
│   ├── abi_i386.c           # i386 SysV: the same (both reached through the target descriptor)
│   ├── legalize.c           # IR legality checks; expands EXT (integer conversions) for the target
│   ├── ssa_dump.c           # SSA -> the ir/ text form, for cc -fdump-ir
│   ├── passes/opt.c         # Optimization passes
│   └── ssa/                 # SSA module/function/block/instruction infrastructure
├── backend/
│   ├── regalloc.c           # Register allocation and spill planning
│   ├── frame.c              # Stack frame layout and slot compaction
│   └── emit_s.c             # GAS-style assembly emission
├── include/                 # Internal driver/frontend/backend headers
├── ir/                      # The textual SSA IR (typed, LLVM-like; tests/usr.bin/cc/valid.ir)
│   ├── ir.c / ir.h          # Module representation
│   ├── parse.c              # Text -> module
│   ├── verify.c             # CFG and SSA invariants (terminators, phi arity, dominance, reachability)
│   │                        # and the opcode table: each opcode's form and its operands' type classes,
│   │                        # and for neutral IR (`stage neutral`) the integer width rules
│   ├── serialize.c          # Module -> text, optionally with value names normalized
│   ├── ir-verifier.c        # Tool: parse and verify
│   ├── ir-normalize.c       # Tool: parse, verify, print normalized
│   └── ir-diff.c            # Tool: compare two files after normalizing
│                            # (cc -fdump-ir writes this form: middle/ssa_dump.c)
├── README.md                # Current capability/status overview
├── SPEC.md                  # Functional requirements
├── Makefile                 # Native/target build-mode split and utility builds
└── docs/                    # refactor-plan.md, neutral-ir-plan.md, language-tasklist.md

tests/usr.bin/cc/
├── conformance_c99/         # C99 conformance inputs
├── diff_c99/                # Differential comparison inputs
├── native_*.c               # Positive and negative compiler coverage corpus
├── run_*.sh                 # Mode matrices, preprocessor, post-C99, ABI, and SSA scripts
├── ir/                      # Golden IR: NAME.c and its -fdump-ir at -m64/-m32 (run_ir_golden.sh)
└── invalid_*.ir / valid*.ir # IR verifier fixtures
```

## 2. High-Level System Diagram

```text
C Source
  -> preprocessing
  -> lex / parse / sema
  -> AST -> SSA IR
  -> optimization + legalization
  -> instruction selection / regalloc / frame lowering
  -> GAS-style assembly (.s)
  -> as
  -> ELF object (.o)
  -> ld
  -> executable / shared object

Driver control plane:
  cc resolves sibling tools first (../as/as, ../ld/ld),
  then falls back to PATH tools or explicit environment overrides.
```

## 3. Core Components

### 3.1. Driver And Toolchain Orchestration

Name: `cmd/cc.c` and `cmd/pipeline.c`

Description: The driver parses user-facing flags, chooses stage stop points (`-E`, `-S`, `-c`, full link), resolves companion tools, and coordinates subprocess execution. It is the boundary between pure compilation logic and host/target toolchain integration.

Technologies: C, fork/exec, argument-vector construction, mode-aware tool resolution

Deployment: Installed as `usr/bin/cc`; `cpp` is exposed as a symlink to the same binary

### 3.2. Frontend

Name: Preprocessor, lexer, parser, and semantic analysis

Description: The frontend turns C translation units into typed ASTs. `frontend/preproc.c` enforces macro-expansion bounds, `frontend/parser.c` owns recursive-descent parsing, and `frontend/sema.c` applies type and language rules before lowering begins: it records every expression's type, makes every implicit arithmetic conversion an explicit cast (operands, `?:` arms, assignments, initializers, returns, arguments and their default promotions), and folds every `sizeof` whose size is known, so lowering reads types and conversions and never works them out. `frontend/types.c` is the one place the size and alignment of a scalar type are decided, from the target description in `common/target.c` (`cc_target_select()` in the pipeline picks it); structure layout, `sizeof`, `_Alignof`, lowering and the emitter all ask it. `frontend/consteval.c` evaluates integer constant expressions (array bounds, enumerators, case labels, `_Static_assert`, static initializers) for the parser, sema and lowering alike; the parser and sema each supply the size of a `sizeof` operand as they know it, and lowering, which sees only folded ones, supplies none. `frontend/nested.c` says what a GNU nested function sees of the function it is defined in (sema puts those variables in an outer scope; lowering passes an array of their addresses in the static chain register, `%r10` or `%ecx`, when the nested function uses them, and its address is a trampoline built in the caller's frame). `frontend/decimal.c` reads decimal floating constants and encodes them in BID, gcc's encoding on x86, for the parser's literals and sema's constant conversions; the decimal types themselves are structures holding that encoding, operated on by libgcc's `__bid_*` functions.

Technologies: C, recursive-descent parsing, bounded macro expansion, semantic diagnostics

Deployment: Linked directly into the `cc` binary

### 3.3. Middle-End And SSA Infrastructure

Name: AST-to-IR lowering, legalization, optimization, and shared SSA utilities

Description: `middle/ast2ir.c` lowers the frontend AST into the compiler's SSA-like IR. It does not decide how an aggregate crosses a call: it asks the selected target (`cc_abi_aggregate_arg()`, `cc_abi_aggregate_return()` in `include/cc_abi.h`, the descriptor's hooks), and `middle/abi_x86_64.c` (the psABI eightbyte classification) or `middle/abi_i386.c` (the stack, with gcc's vector registers) answers with the ABI byte the emitters act on; `va_arg` is theirs too (`cc_abi_va_arg()`), built with `middle/irbuild.c`, the IR builder lowering also uses. The trampoline a nested function's address needs, the full fence, the va_list record `va_copy` duplicates and which returned aggregates the emitter reads through their address are descriptor data, so lowering never asks which target it is compiling for. `middle/legalize.c` and `middle/passes/opt.c` normalize and optimize that IR so backend selection runs on constrained, verifiable input.

Technologies: C, SSA-style IR, verifier-driven invariants, simple optimization passes

Deployment: Shared between `cc` and standalone IR utilities (`ir-verifier`, `ir-normalize`, `ir-diff`)

### 3.4. Backend

Name: Instruction selection, register allocation, frame layout, and assembly emission

Description: The backend converts legalized IR into GAS-style assembly for supported ABIs. `backend/emit_s.c` selects instructions directly from the SSA and writes the `.s` stream, for x86-64 and i386 both; `backend/regalloc.c` chooses which register to spill and `backend/frame.c` checks and aligns frame sizes for it.

Technologies: C, target-specific lowering for i386/x86_64, linear-scan style allocation, textual assembly emission

Deployment: Final in-process compilation stage before the driver invokes `as`

## 4. Data / Persistent Artifacts

### 4.1. Input Translation Units

Name: C source, headers, and preprocessor inputs

Type: Plain text source

Purpose: Provide the compiler with the language surface consumed by the frontend.

### 4.2. In-Memory IR

Name: AST, semantic state, SSA modules/functions/blocks, verifier metadata

Type: Process-local compiler data structures

Purpose: Carry the program through parsing, semantic validation, optimization, and backend lowering.

### 4.3. Intermediate Assembly

Name: GAS-style `.s` output

Type: Text assembly

Purpose: Bridge the native compiler backend to the in-tree assembler without inventing a second object writer inside `cc`.

### 4.4. Final Toolchain Outputs

Name: ELF objects, executables, and shared objects

Type: ELF files produced by `as` and `ld`

Purpose: Deliver artifacts compatible with the Substrate runtime and host-native validation workflows.

## 5. External Integrations / APIs

`as` and `ld`: The driver resolves sibling in-tree binaries first so host-native builds can exercise the full Substrate toolchain without installation.

Host preprocessor: Preprocessing support currently relies on a system `cpp` path for full macro/front-end coverage.

`--bootstrap-gcc`: A temporary compatibility path for source programs outside the currently supported native language slice.

Specifications: Functional requirements live in `SPEC.md`; capability/status notes live in `README.md`.

Tests: Compiler-facing regression surfaces live in `tests/usr.bin/cc/`.

## 6. Build, Installation & Invocation

Host/native mode: `make -C usr.bin/cc NATIVE_BUILD=1` builds `cc`, `cpp`, `ir-verifier`, `ir-normalize`, and `ir-diff` as host-runnable binaries.

Target mode: The default build links against Substrate CRT and libc and brands the output for the operating system image.

Build-mode isolation: `.build-mode` forces object and binary cleanup when switching between native and target builds so incompatible artifacts are not reused across modes.

Tool resolution order: environment override (`AS`, `LD`), sibling build-tree tool, same-directory tool, then PATH fallback.

## 7. Security Considerations

Recursive toolchain guard: The compiler participates in the same bounded toolchain-depth tracking as the assembler so shelling out across `cc`, `as`, and `ld` cannot recurse forever.

Preprocessor limits: Expansion depth, pass count, expanded text size, and expanded token count are bounded to cap hostile macro workloads.

Parser depth: Recursive parse depth is explicitly capped to prevent stack blowups from pathologically nested expressions or statements.

IR verification: Dedicated verifier utilities and legalization passes keep malformed or partially formed IR from silently reaching the backend.

Mode isolation: The `.build-mode` stamp prevents subtle native/target object reuse bugs, which are a correctness and diagnostic hazard during toolchain development.

## 8. Development & Testing Environment

Local build: `make -C usr.bin/cc NATIVE_BUILD=1`

Primary regression surface: `tests/usr.bin/cc/` contains positive/negative compile fixtures, conformance corpora, differential comparison suites, preprocessor mode scripts, ABI mode matrices, and IR verifier fixtures.

Standalone tools: `ir-verifier`, `ir-normalize`, and `ir-diff` make IR regressions easier to isolate than end-to-end compile failures alone.

Integration role: The compiler is the coordinator for real-world toolchain tests because it drives `as` and `ld` together on larger inputs.

## 9. Future Considerations / Roadmap

Structure: `docs/refactor-plan.md` is the order of work for the compiler as a whole -- one type representation, a typed AST from sema, one constant evaluator, a context in place of globals -- and `docs/neutral-ir-plan.md` for the IR and back ends: typed IR values, per-target ABI and legalization passes, thin emitters, with the `ir/` text form as the IR's dump format.

Correctness: open requirements are tracked in `docs/cc-audit.md` at the top of the tree; the language-level goals left from the original bring-up (building the kernel and bash with this cc, as, ld) are in `docs/language-tasklist.md`.

## 10. Project Identification

Project Name: Substrate Native C Compiler

Repository Path: `usr.bin/cc/`

Primary Consumers: Direct user compilation, in-tree userland builds, external host-native package validation

Date of Last Update: 2026-04-22

## 11. Glossary / Acronyms

AST: Abstract Syntax Tree

SSA: Static Single Assignment

IR: Intermediate Representation

ABI: Application Binary Interface

Bootstrap GCC: Temporary fallback path that routes unsupported native compilation workloads through the host GCC toolchain