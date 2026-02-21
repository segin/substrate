# as.x86 Specification

## 1. Purpose and Scope
`as.x86` is the Substrate system assembler for i386 and x86_64.
It translates assembly source into ELF relocatable objects (`ET_REL`) through `libelfobj`.

Primary goals:
- Correct assembly for x86 and x86_64 with complete ISA coverage via backend toolchain integration.
- Deterministic relocation and symbol emission.
- BSD-friendly behavior with a GNU-compatible subset where practical.
- Integration with `ld.x86` and existing toolchain flows.

Out of scope for phase 1:
- Full standalone encoder parity without backend delegation.
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

## 6. Assembly Backend
`as.x86` is a driver that forwards source to the host GCC/GAS backend for both `-32`
and `-64` modes. This yields complete practical x86 ISA coverage (including modern
extensions supported by the host toolchain).

Behavior:
- `as.x86` normalizes options (`-I`, `-D`, `-march`, `-mtune`, `-Wa`, `-g`) and
  forwards unknown assembler flags for compatibility.
- Backend invocation uses `gcc -c -x assembler -m32/-m64`.
- After assembly, output is validated through `libelfobj` to enforce:
  - `ET_REL`
  - i386 ELF32 for `-32`
  - x86_64 ELF64 for `-64`

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
  -> as.x86 CLI normalization
  -> GCC/GAS backend (full parser+encoder)
  -> ELF object (.o)
  -> libelfobj validation gate
  -> ET_REL
```

Modules:
- `driver`: option parsing, mode selection, backend argv construction.
- `backend`: host GCC/GAS invocation.
- `verify`: `libelfobj` class/machine/type validation and diagnostics.

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
- Assemble a real `main()` generated by GCC for i386 and x86_64.
- Assemble with `-march` and `-Wa` forwarding.
- Round-trip through `ld.x86`.
- Final `gcc -s` link smoke test for produced code paths.

Verification tools:
- `readelf -a`, `objdump -dr`, internal `libelfobj` validation.

## 14. Failure Modes and Mitigations
- Instruction form ambiguity -> require explicit suffix/register width.
- Relocation overflow -> hard error before file output.
- Include cycle -> detected via include stack guard.
- Symbol redefinition conflicts -> deterministic diagnostic and fail.

## 15. Maintenance Guidelines
- Keep CLI behavior stable and compatible with existing compiler-driver usage.
- Treat backend forwarding and libelfobj validation as the compatibility contract.
- Add ABI-facing behavior changes only with explicit compatibility notes.
- Treat output object reproducibility as a regression gate.
