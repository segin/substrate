# Architecture Overview

This document is the living architecture baseline for the Substrate native assembler in `usr.bin/as/`.
It describes how the assembler is organized, how it fits into the native toolchain, and which guardrails keep hostile or malformed inputs bounded.

## 1. Project Structure

```text
usr.bin/as/
├── as.c                     # The program: options, the source read and expanded (macros, repeats,
│                            #   conditionals, includes), the passes in their order, listings, messages
├── as_lexer.c               # Tokens; the one scanner of strings and comments
├── as_expr.c                # Expressions: parsed from tokens, evaluated, reduced to a symbol and a number
├── as_parser.c              # Statements: labels, directives, instructions and their operands
├── as_x86_regs.c            # The x86 registers: one table of name, class, width, number
├── as_reloc_op.c            # The relocation operators (@GOT, @PLT, ...): one table, i386 and x86-64
├── as_lower.c               # Directives made the few the rest stores; ARM's own made x86's (as_arm_lower)
├── as_util.c                # The helpers every file had its own of: xstrdup, streq_ci, trim_copy, as_errf
├── as_symtab.c              # Symbols: definition, binding, type, aliases
├── as_sections.c            # Sections, subsections, and the one tracker of which section a source is in
├── as_data.c                # What data directives are written in: string literals, LEB128
├── as_elf_emit.c            # The object writer: its phases, the relocations, the data and text of each section
├── as_layout.c              # Where everything is: sections, offsets, labels, the sizing of branches
├── as_symbols.c             # Where each symbol is, and the object's symbols made of them
├── as_x86_stmt.c            # x86: a statement made its bytes -- selection, refusals, the emitters by form
├── as_flat.c                # The writer of a flat binary (-O binary)
├── as_emit_int.h            # What those five share: the emitter's context and what each calls of the others
│                            #   and the flat image
├── as_cfi.c                 # .cfi_* made .eh_frame or .debug_frame
├── as_dwline.c              # .file and .loc made .debug_line
├── as_x86_vec.c             # x86: the one emitter of VEX, EVEX, MMX and SSE instructions
├── as_x86_vec_table.c       # x86: its table, a row for each form
├── as_x86_vec_table_ext.h   # x86: more rows of it -- FP16, AVX10.2, XOP, AMX, 3DNow! and the rest GNU as has
├── as_x86_vec_table_apx.h   # x86: and APX's forms of the instructions of general registers (generated)
├── as_x86_vec_table_base.h  # x86: and the forms those instructions always had, as they come to the table (generated)
├── as_x86_memsize_table.h   # x86: the sizes of memory each instruction takes in Intel syntax (generated)
├── as_arm.c                 # ARM (A32) encoder; what the ARM and AArch64 encoders share
├── as_arm_simd.c            # ARM: VFP and NEON
├── as_arm_thumb.c           # ARM: Thumb-2
├── as_a64.c                 # AArch64 encoder
├── as_a64_simd.c            # AArch64: floats and vectors
├── *.h                      # Each .c has the header of its name, with its interface (as_cfi.h,
│                            #   as_data.h, as_dwline.h, as_elf_emit.h, as_expr.h, as_lexer.h, as_lower.h,
│                            #   as_parser.h, as_reloc_op.h, as_sections.h, as_symtab.h, as_x86_regs.h,
│                            #   as_x86_encode.h, as_x86_vec.h).
│                            #   Three have no .c of theirs:
│                            #   as_arm.h, the interface of the five ARM and AArch64 files; as_arm_int.h,
│                            #   what those share among themselves; as_strmap.h, a table from names to
│                            #   numbers, whole in the header.
├── Makefile                 # Native/target build wiring, alias links, elfobj dependency
└── TASKLIST_AS.md           # The first task list, closed; what is to do is docs/as-tasks.md

tests/usr.bin/as/
├── run-suite.sh             # Runs every test_*.sh against an assembler it builds for the host,
│                            #   out of the tree; the one thing to run
├── xfail.list               # The tests expected to fail, each with its reason; the suite fails
│                            #   if one of them passes
├── test_*.sh                # Some 165 tests, each a program that says ok or FAIL.  Most assemble
│                            #   source and hold the object to bytes that GNU as or llvm-mc made,
│                            #   and say in their heads whether GNU as passes them
├── test_*_core.c            # Unit tests of one module, built and run by the .sh of their name;
│                            #   lex_file.h is what those that begin from a file share
├── test_gnu_differential.sh # Every line of two corpora against GNU as, with gnu-diff.py; the
│                            #   lines that differ are counted and named in corpus/*.baseline
├── test_insn_forms.sh       # x86 instruction forms, 1,447 cases
├── test_vec_forms.sh        # The VEX/EVEX/SSE table: vec_forms.txt and vec_rules.txt, 16,000 forms
├── test_arm_forms.sh        # ARM, Thumb, AArch64: arm_forms.txt and arm_own.txt, 885 statements
├── test_arm_targets.sh      # The ARM and AArch64 objects, refusals, literal pool, directives
├── test_cli_help.sh         # --help and as(1) held to the options and directives there are
├── test_fuzz_matrix.sh      # Fuzz-smoke and matrix orchestration
├── branch-relax-check.sh, sanitizer_sweep.c, sanitizer_sweep.h   # Shared by several tests
├── compiler/                # Sources a C compiler's output is assembled from
└── corpus/                  # The corpora, their generators and their baselines
```

### The passes

`run_native_backend()` in `as.c` runs them, in this order, each on what the one before left:

1. **Read.** The source, or the output of the C preprocessor for a `.S` file; several sources are one.
2. **Expand.** Macros and `.irp` (`expand_gas_source_controls()`), repeats (`expand_rept_file()`), conditionals (`filter_gas_conditionals()`), all on text, each line carrying a mark of the file and line it came from.
3. **Lex** (`as_lex_text()`): tokens, with `.include` read where an expansion wrote one.
4. **Parse** (`as_parse_tokens()`): statements; numeric labels given names.
5. **Lower.** `as_arm_lower()` for ARM and AArch64, then `as_directives_lower()`: the many directives made the few, numbers settled, `.struct` and common symbols resolved.
6. **Validate** (`validate_directives()`): a directive that is none there is, is refused; `.error`, `.warning` and `.print` are said.
7. **Order.** Numbered subsections put behind their sections (`as_sections_gather_subsections()`); branches to labels of other sections marked (`as_elf_mark_far_branches()`).
8. **Tables.** Symbols (`as_symtab_build()`), sections (`as_sections_build()`).  (Data directives have no table: the emitter emits them from the statements.)
9. **Emit** (`as_elf_emit_file()`, or `as_elf_emit_binary_file()` for `-O binary`): branches relaxed, every statement placed, instructions encoded, relocations made, call frames and the line table built, the object written through libelfobj.
10. **Check** (`validate_output_file()`): the object is read back and is of the machine and class asked for.

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

`as_lexer.c` is the one place a token's end is decided. A statement is a row of tokens -- a token for each operator, punctuation mark, register, number, name and string (`a+b` is three, `%fs:0` three, `$sym` two) -- with the labels, the directive or the mnemonic marked at its head. `as_parser.c` builds operands from those tokens and does not look inside a token's text; `as_expr.c` parses an expression from a row of tokens, and text that holds an expression (a directive's argument, kept as text for what reads the directive) is made tokens by the same lexer first. An operand's `raw` and a directive's arguments are the tokens as they were written, kept for messages and for the consumers that still take text.

There is one reading of an x86 instruction, and it is AT&T's. A statement written in Intel syntax is made the AT&T statement it is at the end of `parse_instruction()` (`intel_statement_to_att()`): its operands put in the other order (but for the few that are the same in both, `enter` and `bound` among them), a register's `raw` made `%eax`, a number's `$5`, what a jump or call goes through `*%eax`, and the x87's swapped pair renamed (`fsubp st(1), st` is AT&T's `fsubrp`). `syntax_intel` is 0 on the statement that results and `from_intel` is 1. What is left of the Intel way is the size written on memory (`as_mem_operand_t.size_bits`), which AT&T says in the mnemonic: `encode_x86_stmt()` makes the suffix of it where the instruction has one (`x86_sized_mnemonic()`: `shl` and `BYTE PTR` are `shlb`, `fld` and `QWORD PTR` are `fldl`), and refuses memory with no size where the instruction would take more than one. An instruction of vectors has no suffix, and the size on its memory is held to `as_x86_memsize_table.h`: the sizes GNU as takes of each name, by the widest register of vectors among its operands (`x86_memory_size_refused()`). A `d` after a name is Intel's for AT&T's `l`, and is tried where the name as it stands is no instruction of the operands. A name in quotes among an instruction's operands is marked (`as_token_t.quoted`) and is a symbol whatever it spells.

Technologies: C, bounded token buffers, expression parsing, section and symbol bookkeeping

Deployment: Linked directly into the single `as` binary

### 3.3. ISA Encoding Backends

Name: Architecture-specific instruction encoders and relocation helpers

Description: ISA families are split into dedicated files such as `as_x86_vec.c`, `as_arm.c`, and `as_a64.c`. The parser remains mostly architecture-agnostic and hands normalized instruction/data requests into the active encoder backend.

**ARM and AArch64: the decision.** The two back ends are completed, not removed (2026-10-11). There were seventeen files, `as_arm_*.c` and `as_a64_*.c`, that nothing called and that were fixtures: a table of one fixed word for a mnemonic, whatever its operands. They are gone, and five files written in their place that read the operands. Run as `arm-as` or `aarch64-as`, or with `--target`, the assembler writes an `EM_ARM` or an `EM_AARCH64` object.

How they fit, since the rest of the assembler was written for x86:

- *An operand is its text.* An operand of these machines -- `[r1, #4]!`, `{r4-r7, lr}`, `r2, lsl #3`, `v0.s[1]`, `:lo12:sym`, `=0x1234` -- is none of the kinds the x86 front end knows, so in this mode the parser keeps each operand as written (`AS_OPERAND_RAW`, with no blank in it but one between two words) and the encoder reads it. The mnemonic is whole too: its `s` and condition are split off by the encoder's table of the real mnemonics (`as_arm_split_mnemonic()`), in either order; the parser's own copy of that, which took `teq` for `t` and `eq`, is gone.
- *One call per instruction.* `as_arm_encode()` and `as_a64_encode()` (`as_arm.h`) are handed the mnemonic, the operands, where the instruction stands, whether it is Thumb and in an IT block, and a callback that says where a label of its section is. They answer with the bytes and, for each field the linker fills, the ELF relocation's own number, the symbol and the addend. The emitter calls them at its two places an instruction is encoded -- to measure (`append_virtual_instruction_bytes()`, no labels known) and to write (`encode_x86_stmt_for_layout()`) -- and adds the relocations in `emit_relocations()`. An instruction is as long wherever it stands, so nothing is relaxed; a Thumb branch is 32 bits unless `.n` is written.
- *The data directives are x86's.* `as_elf_cfg_t.target` is `EM_ARM` or `EM_AARCH64` while `machine` stays the x86 machine of the same ELF class, by which sections, symbols and data are laid out as ever; the relocation of a data directive is chosen as x86's and named as the target names it where it is written (`reloc_type_for_target()`). ARM's addends are in place (REL) as i386's are, AArch64's in the relocation (RELA) as x86-64's. The code mode that `.code16`/`.code32` set is Thumb/ARM here.
- *What is ARM's own is lowered first.* `as_arm_lower()` (`as_lower.c`) runs before `as_directives_lower()`: `ldr reg, =value` becomes a move or a load from a literal pool, placed at `.ltorg`/`.pool` and at the end of its section; `.word`, `.inst`, `.align`, `.arm`, `.thumb`, `.code`, `.thumb_func`, `.eabi_attribute` become what the rest knows; `.syntax`, `.arch`, `.cpu`, `.fpu` are checked and dropped; the unwinding directives (`.fnstart` and the rest) are refused, no table being made. The scanner's idea of a comment is the target's (`as_scan_set_dialect()`).

Not done: mapping symbols (`$a`, `$t`, `$d`), ARM's unwinding tables, relaxation of Thumb branches, and the instructions outside the classes named in as(1). `tests/usr.bin/as/test_arm_forms.sh` holds 885 statements to llvm-mc's bytes, and `test_arm_targets.sh` the objects, the refusals, the pool and the directives, and runs a program of each machine under qemu.

Call frame information is `as_cfi.c`: the emitter gathers the `.cfi_*` directives with the section and offset each stands at, and is handed back the bytes of `.eh_frame` or `.debug_frame` and the places in them a relocation fills (`emit_call_frames()` in `as_elf_emit.c`). `tests/usr.bin/as/test_cfi_frames.sh` holds them to GNU as's bytes.

The line table is `as_dwline.c`: the emitter gathers the files of `.file N` and a row for each `.loc`, placed where it takes effect (`line_rows()`), and is handed back the bytes of `.debug_line` (`emit_line_table()`). The name a `.loc ... view` gives is a number wherever one is written (`line_view_value()`). With `-g` and no `.loc` in the source the rows are its instructions, and a compile unit is made for them. Where every statement stands is found for all of them by one walk (`layout_offsets()`), which the rows and the frames both use. `tests/usr.bin/as/test_line_table.sh`.

The x86 instructions with a VEX or an EVEX prefix, and those of MMX and SSE that have neither, are one table and one emitter. `as_x86_vec_table.c` has a row for each form of each instruction: its name, the kinds of its operands in AT&T order and where each goes (ModRM.reg, ModRM.rm, `vvvv`, an immediate byte or its high half), the prefix, map, opcode, `W` and length, what a one-byte displacement is multiplied by, whether memory may be broadcast, and what may be written in braces. `as_x86_vec.c` finds the rows of a name by one lookup, takes the first whose operands are the statement's -- VEX rows stand before EVEX ones, so what AVX can say is not said in AVX-512 -- and writes it; the level `-march` must allow is the row's. `encode_x86_vec()` in `as_elf_emit.c` asks it before any other encoder, and a name the table has is refused by it and by nothing else. `tests/usr.bin/as/test_vec_forms.sh` holds 76,000 forms to GNU as's bytes. `as_x86_vec_table_ext.h` is more rows of the same table, included at its end: the instruction sets GNU as 2.47 has and the audit's corpus had nothing of (AVX512-FP16, AVX10.2, XOP, TBM, LWP, AMX, CMPccXADD, Key Locker, 3DNow! and the later system instructions), each row made from the bytes GNU as produces for the statement and for its variants with a mask, a broadcast, rounding and a displacement; XOP's prefix (`AS_VEC_XOP`) and the row flags `AS_VEC_F_MODRM`, `AS_VEC_F_IMM32`, `AS_VEC_F_SIB` and `AS_VEC_F_SUFFIX` are what those needed of the emitter. The emitter asks nothing else about an instruction with a VEX or an EVEX prefix: the twelve per-extension encoders it tried after the table, and ran again to learn which `-march` a statement wanted, are gone, and the AVX-512 files with them (`as_x86_avx512f.c`, `as_x86_avx512bw.c`, `as_x86_avx512dq.c`, `as_x86_evex.c`). Those of the older extensions went after them (`as_x86_avx.c`, `as_x86_avx2.c`, `as_x86_fma.c`, `as_x86_bmi1.c`, `as_x86_bmi2.c`, `as_x86_v2.c`, `as_x86_v3_misc.c`): `as_x86_encode.c` asked eleven of them about every instruction, and the table has every name they had but `fisttp`. `as_x86_vex.c` writes the VEX prefix of the mask-register instructions (`encode_x86_opmask()`).

APX's forms of the instructions of general registers are rows of the same table (`as_x86_vec_table_apx.h`, map 4): a new destination in vvvv (`AS_VEC_F_ND`), no flags written (`AS_VEC_F_NF`), a condition in the prefix (`AS_VEC_F_SCC`). Their names are instructions assembled elsewhere, and their first row says so (`AS_VEC_F_APX`): `as_x86_vec_knows()` does not count them, `encode_x86_vec()` asks the table of them first in 64-bit code, and where no row is of the operands -- `add %ecx, %edx` -- the instruction goes where it always did.

The forms those instructions always had are coming to the same table a family at a time (`as_x86_vec_table_base.h`, rows of `AS_VEC_F_BASE`, generated from what GNU as makes of each name). `encode_x86_vec()` asks the table for such a name in every mode. A name some of whose forms are rows falls back, where no row takes the operands, to the encoders that compare names (`as_x86_encode_i386()`, `as_x86_encode_x86_64()` and the emitters of `as_x86_stmt.c`). A name all of whose forms are rows is marked so (`AS_VEC_LEGACY_WHOLE`, the macro `LW`; `as_x86_vec_base_is_whole()`): the table answers alone, what no row takes is refused there, and the name's branches in those encoders are deleted. A row of this kind has no VEX: its opcode, a `/digit` or a register in ModRM or in the opcode (role `+`), the prefix of its operand's size (`AS_VEC_F_OP16`, `AS_VEC_F_OP32`), and a number of one byte, of the two or four the row says, or of the code's own size where it says both. For these rows the table also writes a `lock` (after the prefixes of size, and only where the instruction writes to memory), xacquire and xrelease, and the other of an instruction's two ways for `{load}`.

Technologies: C, per-ISA opcode tables and helper logic, relocation fixup synthesis

Deployment: Statically linked backend modules selected at runtime by target mode

### 3.4. Relaxation And ELF Emission

Name: Relaxation pass and object-file emission

Description: Branch relaxation is done in `as_elf_emit.c` by `relax_branches()`: every `jmp` and conditional jump of 32- or 64-bit code to a label of its own section (numbered, compiler's or the source's own, or `.`, with or without a number added) starts in its two-byte form, the statements are laid out, each jump that does not reach is made long, and that repeats until none changes. The decisions are one table (`branch_short`) that both the layout and the encoding read, so the two cannot disagree. 16-bit code, calls and branches to other sections or files are not sized there. (A separate relaxation module existed that nothing called and that sized every instruction as four bytes; it is removed.) `as_elf_emit.c` then serializes sections, symbols, and relocations through `libelfobj` into the final ELF relocatable object.

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

Specifications: Behavioral requirements live in `docs/specs/as_spec.md`; what remains to do is `docs/as-tasks.md`, each task with the test or inspection that closes it (`TASKLIST_AS.md`, the list before it, is closed).

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

Parity growth: `docs/as-tasks.md` tracks what is open, among it the one table of templates for x86 (AS-T-161) that the per-extension files give way to.

Cross-ISA maintenance: The shared parser/front-end must remain architecture-neutral enough to keep x86, ARM, and AArch64 backends aligned without backend-specific parser forks.

Toolchain integration: As the native compiler grows, `as` remains the canonical assembly sink for generated `.s` output and therefore inherits more high-volume, machine-generated input than hand-written assembly alone.

## 10. Project Identification

Project Name: Substrate Native Assembler

Repository Path: `usr.bin/as/`

Primary Consumers: Direct user invocation, `usr.bin/cc`, and toolchain regression harnesses

Date of Last Update: 2026-10-11

## 11. Glossary / Acronyms

ELF: Executable and Linkable Format

ISA: Instruction Set Architecture

VEX / EVEX: x86 encoding prefixes used for AVX and AVX-512 families

Relaxation: Iterative shortening or resizing of encoded instructions once symbol distances are known

Fixup: Deferred value that becomes a relocation or fully resolved encoded field during assembly