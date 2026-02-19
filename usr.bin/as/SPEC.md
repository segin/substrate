# as.x86 Specification

## 1. Purpose and Scope
`as.x86` is the Substrate system assembler for i386 and x86_64.
It translates assembly source into ELF relocatable objects (`ET_REL`) through `libelfobj`.

Primary goals:
- Correct encoding for x86 and x86_64.
- Deterministic relocation and symbol emission.
- BSD-friendly behavior with a GNU-compatible subset where practical.
- Integration with `ld.x86` and existing toolchain flows.

Out of scope for phase 1:
- Full GAS compatibility.
- AVX-512 complete coverage.
- Macro language parity with every historical assembler.

## 2. Standards and Compatibility
- System V ABI.
- i386 ABI and x86-64 psABI relocation/symbol rules.
- DWARF emission for `.debug_line`, `.debug_info`, and minimal `.eh_frame`.
- GNU ELF extensions where required by toolchains.
- BSD semantics preferred where GNU behavior is ambiguous.

## 3. Functional Requirements (EARS)
### Ubiquitous
- U1: The assembler SHALL translate valid assembly into correct machine code.
- U2: The assembler SHALL emit correct relocation records.
- U3: The assembler SHALL preserve section alignment constraints.
- U4: The assembler SHALL support local and global labels.

### Event-driven
- E1: When an undefined symbol is referenced, the assembler SHALL emit relocation.
- E2: When an invalid instruction is encountered, the assembler SHALL produce diagnostic output and non-zero exit.
- E3: When expression or encoding constraints overflow relocation width, the assembler SHALL emit an error.

### Security
- S1: The assembler SHALL validate numeric expressions for overflow and illegal operations.
- S2: The assembler SHALL bound-check section buffers and reject unsafe growth.

## 4. CLI and Driver Behavior
Canonical invocation:
```
as.x86 -o file.o file.s
```

Supported options:
- `-32`: force i386 target.
- `-64`: force x86_64 target.
- `-g`: emit debug information.
- `-I <dir>`: include directory for `.include`.
- `-D <name[=value]>`: predefine symbol/macro.
- `-Wa <opts>`: pass assembler options from compiler drivers.
- `-march <cpu>`: ISA feature level selection.
- `-mtune <cpu>`: scheduling hint metadata (no encoding effect in phase 1).
- `-o <path>`: output object path.

Exit status:
- `0`: success.
- `1`: assembly failed (syntax/semantic/encoding errors).
- `2`: usage/configuration error.

## 5. Source Language
### 5.1 Syntax modes
- AT&T syntax and Intel syntax.
- Mode selected by CLI or source directive (planned extension).

### 5.2 Directives (required)
- `.text`, `.data`, `.bss`
- `.section`
- `.globl`, `.weak`, `.local`
- `.type`, `.size`
- `.align`, `.p2align`
- `.comm`, `.lcomm`
- `.quad`, `.long`, `.byte`
- `.string`, `.ascii`
- `.macro`, `.endm`
- `.if`, `.endif`
- `.include`

### 5.3 Expressions
- Constant folding with signed/unsigned width checks.
- Symbol arithmetic with relocatable term tracking.
- Section-relative expressions with validation.
- Disallow ambiguous multi-symbol relocatable expressions unless backend supports explicit relocation synthesis.

## 6. Instruction Encoding
Table-driven encoding database:
- Mnemonic + operand forms -> encoding template.
- Prefix classes: legacy, REX, VEX, EVEX (EVEX optional).
- ModRM/SIB/displacement/immediate layout synthesis.
- Width inference rules by operand size defaults and suffixes.
- Jump/call relaxation pass for short/near alternatives.

### x86_64-v1 Coverage
For `-64`, phase-1 `as.x86` uses a host backend path (`gcc/as`) targeting
`x86-64` (v1 baseline)
to guarantee full ISA coverage, then validates the produced ELF object through
`libelfobj` before returning success.

## 7. Relocation Model
Required relocation types:
- x86_64: `R_X86_64_64`, `R_X86_64_PC32`, `R_X86_64_PLT32`, `R_X86_64_GOTPCREL`, TLS family.
- i386: `R_386_32`, `R_386_PC32`, TLS family.

Emission rules:
- Undefined or external references create relocation entries.
- PC-relative references use backend `is_pc_relative` semantics.
- Addends are represented through RELA where target ABI requires or prefers them; REL compatibility maintained for i386.

## 8. ELF Integration via libelfobj
### 8.1 API interactions
- Create object: `elf_create(ET_REL, machine, class, endian)`.
- Create/find sections: `elf_add_section`, `elf_find_section`.
- Set section bytes: `elf_section_set_data`.
- Emit symbols: `elf_add_symbol`.
- Emit relocations: `elf_add_relocation`.
- Validate: `elf_validate`.
- Serialize: `elf_write_file`.

### 8.2 Section policy
- Required: `.text`, `.data`, `.bss`, `.symtab`, `.strtab`, `.shstrtab`.
- Optional/generated: `.rela.text`/`.rel.text`, debug sections, note sections.
- Preserve user-requested alignment and flags.

## 9. Internal Architecture
```
Input .s
  -> Lexer
  -> Parser
  -> IR (statements, labels, expressions)
  -> Encoder + Relaxation
  -> Symbol/Relocation Generator
  -> ELF Emitter (libelfobj)
  -> ET_REL
```

Modules:
- `lex`: tokenization, include stack, location tracking.
- `parse`: grammar for directives, instructions, macro flow.
- `expand`: macros and conditional assembly.
- `expr`: expression evaluation and relocatability analysis.
- `encode`: instruction selection + binary emission.
- `sym`: symbol table, scope, binding, `.type/.size`.
- `reloc`: relocation creation and overflow checks.
- `emit`: object assembly through `libelfobj`.
- `diag`: diagnostic formatting and policy.

## 10. Diagnostics
Format:
```
file.s:line:column: error: message
file.s:line:column: warning: message
```

Behavior:
- Continue after recoverable parse errors to report multiple issues.
- Stop immediately for internal consistency violations.
- Optionally include source excerpt and caret indicator.

## 11. Thread Safety and Memory Model
- CLI driver is single-threaded by default.
- Internal state is owned by one compilation unit context.
- No global mutable state except immutable encoding tables.
- `libelfobj` handles are not shared across threads unless externally synchronized.

## 12. Performance Targets
- Linear-time lex/parse over source size.
- Near-linear encoding with bounded relaxation passes.
- Handle large generated assembly files with low per-line overhead.
- Avoid quadratic symbol lookups by using hash-based maps.

## 13. Testing Plan
Required tests:
- Assemble trivial function for i386 and x86_64.
- Assemble PIC code with global symbol relocations.
- Assemble TLS references and verify relocation classes.
- Compare emitted bytes and relocations against system assembler for selected fixtures.
- Round-trip through `ld.x86`.
- Fuzz malformed source and expression edge cases.

Verification tools:
- `readelf -a`, `objdump -dr`, internal `libelfobj` validation.

## 14. Failure Modes and Mitigations
- Instruction form ambiguity -> require explicit suffix/register width.
- Relocation overflow -> hard error before file output.
- Include cycle -> detected via include stack guard.
- Symbol redefinition conflicts -> deterministic diagnostic and fail.

## 15. Maintenance Guidelines
- Add instructions through encoding tables, not ad-hoc conditionals.
- Keep directive semantics in parser and symbol/section modules, not in encoder.
- Add ABI-facing behavior changes only with explicit compatibility notes.
- Treat output object reproducibility as a regression gate.
- Keep host-backend usage constrained and simple (currently `-64` / x86_64-v1)
  while native encoder coverage is expanded.
