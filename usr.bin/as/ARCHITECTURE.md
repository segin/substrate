# Architecture Overview

This document is the living architecture baseline for the Substrate native assembler in `usr.bin/as/`.
It describes how the assembler is organized, how it fits into the native toolchain, and which guardrails keep hostile or malformed inputs bounded.

## 1. Project Structure

```text
usr.bin/as/
├── as.c                     # CLI entry point, toolchain recursion guard, context limits
├── as_lexer.c               # Tokenizer for directives, identifiers, numbers, strings
├── as_parser.c              # Statement/directive parsing and architecture dispatch
├── as_symtab.c              # Symbol definition, lookup, local/numeric label support
├── as_sections.c            # Section creation, switching, alignment, layout state
├── as_data.c                # Data directives, literals, fixups, expression materialization
├── as_relax.c               # Multi-pass branch/jump relaxation
├── as_elf_emit.c            # ELF object emission through libelfobj
├── as_x86_*.c               # x86 and x86_64 encoders, relocations, ISA extensions
├── as_arm_*.c               # ARM encoders, relocations, system/VFP/NEON handling
├── as_a64_*.c               # AArch64 encoders, relocations, SIMD/system handling
├── Makefile                 # Native/target build wiring, alias links, elfobj dependency
└── TASKLIST_AS.md           # Forward backlog for parser/encoder parity work

tests/usr.bin/as/
├── test_*_core.c/.sh        # Unit and driver-facing core tests by subsystem
├── test_cli_*.sh            # CLI and mode behavior
├── test_*_roundtrip.sh      # ISA corpus parity / roundtrip checks
├── test_fuzz_matrix.sh      # Fuzz-smoke and matrix orchestration
└── corpus/                  # Sample assembly corpora for regression inputs
```

## 2. High-Level System Diagram

```text
Assembly Source
  -> lexer / parser
  -> section + symbol + data state
  -> ISA-specific encoder / relocation logic
  -> relaxation passes
  -> libelfobj writer
  -> ELF relocatable object (.o)

Direct user invocation:      source.s -> as -> object.o
Compiler-driven invocation:  cc --from-cc -> as -> object.o
```

## 3. Core Components

### 3.1. Driver And Assembly Context

Name: CLI driver and `as_ctx_t` orchestration

Description: `as.c` owns option parsing, target mode selection, shared assembler limits, and the recursion guard used when `cc` shells out to `as`. It is the component that decides whether the parse/encode path runs in i386, x86_64, ARM, or AArch64 mode.

Technologies: C, in-tree argument parsing, environment-variable guardrails, `libelfobj`

Deployment: Installed as `usr/bin/as`; alias symlinks expose `as.x86`, `as.x64`, `arm-as`, and `aarch64-as`

### 3.2. Front-End Source Model

Name: Lexer, parser, sections, symbol table, and data directives

Description: `as_lexer.c`, `as_parser.c`, `as_sections.c`, `as_symtab.c`, and `as_data.c` turn source text into a bounded in-memory assembly model. This layer owns labels, numeric locals, directives, literals, fixups, include/macro handling, and section-relative state before any ISA-specific encoding occurs.

Technologies: C, bounded token buffers, expression parsing, section and symbol bookkeeping

Deployment: Linked directly into the single `as` binary

### 3.3. ISA Encoding Backends

Name: Architecture-specific instruction encoders and relocation helpers

Description: ISA families are split into dedicated files such as `as_x86_encode.c`, `as_x86_vex.c`, `as_arm_encode.c`, and `as_a64_encode.c`. The parser remains mostly architecture-agnostic and hands normalized instruction/data requests into the active encoder backend.

Technologies: C, per-ISA opcode tables and helper logic, relocation fixup synthesis

Deployment: Statically linked backend modules selected at runtime by target mode

### 3.4. Relaxation And ELF Emission

Name: Relaxation pass and object-file emission

Description: `as_relax.c` reruns bounded size/offset adjustment passes until branch sizing converges or the configured pass limit is reached. `as_elf_emit.c` then serializes sections, symbols, and relocations through `libelfobj` into the final ELF relocatable object.

Technologies: C, bounded relaxation loops, `libelfobj` section/symbol/relocation APIs

Deployment: Final pipeline stage inside `as`

## 4. Data / Persistent Artifacts

### 4.1. Input Translation Units

Name: Assembly source files and included text

Type: Plain text source

Purpose: Provide directives, labels, expressions, and instruction streams for the assembler pipeline.

### 4.2. In-Memory Assembly State

Name: Sections, symbols, fixups, pending relocations, and relaxation metadata

Type: Process-local C structures (`as_ctx_t` and subsystem-owned arrays/buffers)

Purpose: Hold the normalized assembly program while parsing, encoding, and relaxation are still in progress.

### 4.3. Output Object Files

Name: ELF relocatable objects

Type: ELF `.o` written through `libelfobj`

Purpose: Feed the native linker and preserve relocation/symbol information needed by later toolchain stages.

## 5. External Integrations / APIs

`libelfobj`: Used as the only object writer/ELF abstraction layer for section, symbol, and relocation emission.

`cc`: The compiler driver invokes `as` with `--from-cc` and ABI mode flags so C compilation and direct assembly share one assembler implementation.

Specifications: Behavioral requirements live in `docs/specs/as_spec.md`; remaining backlog and parity work live in `TASKLIST_AS.md`.

Tests: Architecture-facing regression surfaces live in `tests/usr.bin/as/`.

## 6. Build, Installation & Invocation

Host/native mode: `make -C usr.bin/as NATIVE_BUILD=1` builds a host-runnable assembler and forces a matching native build of `usr.lib/elfobj`.

Target mode: The default target build links against the Substrate userland and brands the resulting binary for the OS image.

Alias links: The Makefile creates `as.x86`, `as.x64`, `arm-as`, and `aarch64-as` symlinks so the same binary can be invoked with architecture-specific names in scripts and tests.

## 7. Security Considerations

Recursion control: `as.c` tracks toolchain depth via environment variables and rejects runaway `cc -> as -> cc` style invocation loops.

Input bounding: The assembler context enforces limits for total input bytes, per-line bytes, token length, macro depth, and include depth.

Growth checks: Dynamic growth sites in token joining, path handling, byte buffers, and symbol tables use explicit overflow guards before allocation or multiplication.

Relaxation bounds: Relaxation is pass-bounded and cannot spin indefinitely on oscillating branch encodings.

## 8. Development & Testing Environment

Local build: `make -C usr.bin/as NATIVE_BUILD=1`

Primary regression surface: `tests/usr.bin/as/` covers lexer/parser core behavior, directive handling, ELF emission, ISA encoders, CLI behavior, corpus roundtrips, and fuzz-smoke orchestration.

Integration role: The assembler is also exercised indirectly through compiler and linker integration tests, especially when `cc` shells out to `as` during host-native validation.

## 9. Future Considerations / Roadmap

Parity growth: `TASKLIST_AS.md` continues to track ISA feature expansion and compatibility work.

Cross-ISA maintenance: The shared parser/front-end must remain architecture-neutral enough to keep x86, ARM, and AArch64 backends aligned without backend-specific parser forks.

Toolchain integration: As the native compiler grows, `as` remains the canonical assembly sink for generated `.s` output and therefore inherits more high-volume, machine-generated input than hand-written assembly alone.

## 10. Project Identification

Project Name: Substrate Native Assembler

Repository Path: `usr.bin/as/`

Primary Consumers: Direct user invocation, `usr.bin/cc`, and toolchain regression harnesses

Date of Last Update: 2026-04-22

## 11. Glossary / Acronyms

ELF: Executable and Linkable Format

ISA: Instruction Set Architecture

VEX / EVEX: x86 encoding prefixes used for AVX and AVX-512 families

Relaxation: Iterative shortening or resizing of encoded instructions once symbol distances are known

Fixup: Deferred value that becomes a relocation or fully resolved encoded field during assembly