# Substrate Assembler (`as`) — Specification

## 1. Purpose

The Substrate assembler (`as`) is a standalone, multi-architecture assembler that translates assembly language source files into relocatable ELF object files. It replaces the current GCC/GAS wrapper with a native implementation that has no external toolchain dependencies at runtime.

## 2. Scope

| Attribute        | Value                                                    |
|------------------|----------------------------------------------------------|
| Binary name      | `as`                                                     |
| Install path     | `/usr/bin/as`                                            |
| Input            | Assembly source (`.s`, `.S`), AT&T or Intel syntax       |
| Output           | ELF relocatable objects (`.o`), ET_REL                   |
| Architectures    | i386, x86-64 (v1–v4), ARMv7 (AArch32), AArch64 (v8.0–8.1) |
| Library deps     | `libelfobj` (ELF generation)                             |
| Host build       | `NATIVE_BUILD=1` for development/test on Linux/BSD host  |

## 3. Definitions

| Term       | Definition                                                                 |
|------------|----------------------------------------------------------------------------|
| EARS       | Easy Approach to Requirements Syntax (ISO/IEC/IEEE 29148 compatible)       |
| Mnemonic   | Textual instruction name (e.g., `MOV`, `ADD`, `LDR`)                       |
| Directive  | Assembler pseudo-instruction (e.g., `.byte`, `.section`, `.globl`)         |
| Relaxation | Automatic promotion of short encoding to long encoding when target is out of range |
| Mapping symbol | ARM ELF convention: `$a` (ARM code), `$t` (Thumb code), `$d` (data) |
| ISA level  | x86-64 psABI microarchitecture feature level (v1 baseline, v2–v4 extensions) |

---

## 4. Functional Requirements

### 4.1 Input Processing

**REQ-AS-010** *(Ubiquitous)*
The assembler shall accept one or more assembly source files as positional arguments.

**REQ-AS-011** *(Ubiquitous)*
The assembler shall read source input as a stream of lines delimited by newline characters (`\n`).

**REQ-AS-012** *(Event-driven)*
When the `-I dir` option is specified, the assembler shall add `dir` to the include search path for `.include` directives.

**REQ-AS-013** *(Event-driven)*
When the input filename is `-`, the assembler shall read source from standard input.

**REQ-AS-014** *(Ubiquitous)*
The assembler shall support UTF-8 encoding in string literals and comments.

### 4.2 Lexical Analysis

**REQ-AS-020** *(Ubiquitous)*
The assembler shall tokenize input into mnemonics, register names, immediate values, labels, directives, operators, and string literals.

**REQ-AS-021** *(Ubiquitous)*
The assembler shall recognize integer literals in decimal, hexadecimal (`0x` prefix), octal (`0` prefix), and binary (`0b` prefix) notation.

**REQ-AS-022** *(Ubiquitous)*
The assembler shall treat `#` and `//` as single-line comment initiators and `/* ... */` as block comment delimiters.

**REQ-AS-023** *(Ubiquitous)*
The assembler shall recognize `;` as an instruction separator on architectures where this is conventional.

**REQ-AS-024** *(Event-driven)*
When a line ends with `\`, the assembler shall treat the following line as a continuation of the current logical line.

**REQ-AS-025** *(Ubiquitous)*
The assembler shall recognize character literals in the form `'c'` as integer immediates equal to the ASCII value of `c`.

### 4.3 Parsing

**REQ-AS-030** *(Ubiquitous)*
The assembler shall parse each logical line as one of: a label definition, a directive, an instruction, or a blank/comment-only line.

**REQ-AS-031** *(Ubiquitous)*
The assembler shall accept labels ending with `:` at the start of a line, optionally followed by an instruction or directive on the same line.

**REQ-AS-032** *(Ubiquitous)*
The assembler shall support local numeric labels (`0:`–`9:`) with forward (`Nf`) and backward (`Nb`) references.

**REQ-AS-033** *(Ubiquitous)*
The assembler shall evaluate constant expressions containing `+`, `-`, `*`, `/`, `%`, `|`, `&`, `^`, `~`, `<<`, `>>`, and unary `-`.

**REQ-AS-034** *(Ubiquitous)*
The assembler shall resolve symbol references within expressions, deferring unresolved references to relocation emission.

**REQ-AS-035** *(Event-driven)*
When `-msyntax=intel` is specified for an x86 target, the assembler shall parse instructions in Intel syntax (destination-first, no `%` register prefix, no `$` immediate prefix).

**REQ-AS-036** *(Ubiquitous)*
The assembler shall, by default, parse x86 instructions in AT&T syntax (source-first, `%` register prefix, `$` immediate prefix).

### 4.4 Directives

**REQ-AS-040** *(Ubiquitous)*
The assembler shall implement the following data directives: `.byte`, `.short`/`.hword`, `.long`/`.int`, `.quad`/`.8byte`, `.float`, `.double`, `.ascii`, `.asciz`/`.string`, `.zero`/`.space`, `.fill`, `.skip`, `.org`, `.incbin`.

**REQ-AS-041** *(Ubiquitous)*
The assembler shall implement the following section directives: `.text`, `.data`, `.bss`, `.rodata`, `.section`, `.pushsection`, `.popsection`, `.previous`, `.subsection`.

**REQ-AS-042** *(Ubiquitous)*
The assembler shall implement the following symbol directives: `.globl`/`.global`, `.local`, `.weak`, `.comm`, `.lcomm`, `.type`, `.size`, `.hidden`, `.protected`, `.internal`, `.symver`.

**REQ-AS-043** *(Ubiquitous)*
The assembler shall implement the following alignment directives: `.align`, `.balign`, `.p2align`.

**REQ-AS-044** *(Ubiquitous)*
The assembler shall implement the following conditional directives: `.if`, `.ifdef`, `.ifndef`, `.else`, `.elseif`, `.endif`.

**REQ-AS-045** *(Ubiquitous)*
The assembler shall implement the following macro directives: `.macro`/`.endm`, `.rept`/`.endr`, `.irp`/`.irpc`.

**REQ-AS-046** *(Ubiquitous)*
The assembler shall implement the following debug directives: `.file`, `.loc`, `.cfi_startproc`, `.cfi_endproc`, `.cfi_def_cfa`, `.cfi_def_cfa_offset`, `.cfi_def_cfa_register`, `.cfi_offset`, `.cfi_restore`, `.cfi_remember_state`, `.cfi_restore_state`, `.cfi_adjust_cfa_offset`, `.cfi_rel_offset`, `.cfi_register`, `.cfi_undefined`, `.cfi_same_value`, `.cfi_escape`, `.cfi_personality`, `.cfi_lsda`, `.cfi_return_column`, `.cfi_signal_frame`.

**REQ-AS-047** *(Ubiquitous)*
The assembler shall implement `.group` for COMDAT section groups.

**REQ-AS-048** *(Event-driven)*
When the `.include "file"` directive is encountered, the assembler shall insert the contents of the named file at the current position, searching `-I` paths.

### 4.5 Architecture Selection

**REQ-AS-050** *(Ubiquitous)*
The assembler shall determine the target architecture from the `-march` option or the `--32`/`--64` flags.

**REQ-AS-051** *(Event-driven)*
When `--32` is specified, the assembler shall target i386 (ELF32, EM_386).

**REQ-AS-052** *(Event-driven)*
When `--64` is specified, the assembler shall target x86-64 (ELF64, EM_X86_64).

**REQ-AS-053** *(Event-driven)*
When `-march=armv7-a` or similar ARMv7 variant is specified, the assembler shall target ARMv7 (ELF32, EM_ARM).

**REQ-AS-054** *(Event-driven)*
When `-march=armv8-a` or similar AArch64 variant is specified, the assembler shall target AArch64 (ELF64, EM_AARCH64).

**REQ-AS-055** *(Event-driven)*
When `-march=x86-64-v2`, `-march=x86-64-v3`, or `-march=x86-64-v4` is specified, the assembler shall accept all instructions defined by that ISA level and all lower levels.

**REQ-AS-056** *(Unwanted behavior)*
If an instruction requires an ISA level higher than the selected `-march`, then the assembler shall emit a diagnostic error identifying the instruction, the required level, and the current level.

**REQ-AS-057** *(State-driven)*
While targeting an ARM architecture, the assembler shall accept `.arm`, `.thumb`, and `.thumb_func` directives to switch instruction encoding mode.

### 4.6 x86 Instruction Encoding

**REQ-AS-060** *(Ubiquitous)*
The assembler shall encode all i386 base ISA instructions using legacy prefix + opcode + ModR/M + SIB + displacement format.

**REQ-AS-061** *(State-driven)*
While targeting x86-64, the assembler shall emit REX prefixes for 64-bit operands, extended registers (R8–R15), and RIP-relative addressing.

**REQ-AS-062** *(State-driven)*
While targeting x86-64-v3 or higher, the assembler shall emit 2-byte or 3-byte VEX prefixes for AVX, AVX2, BMI, FMA, and F16C instructions.

**REQ-AS-063** *(State-driven)*
While targeting x86-64-v4, the assembler shall emit 4-byte EVEX prefixes for AVX-512 instructions, including opmask register encoding, zeroing-masking, embedded broadcast, and static rounding mode.

**REQ-AS-064** *(Ubiquitous)*
The assembler shall select the shortest valid encoding for each instruction (e.g., prefer 2-byte VEX over 3-byte VEX when possible).

**REQ-AS-065** *(Unwanted behavior)*
If an immediate value or displacement exceeds the encoding width of the instruction format, then the assembler shall emit a diagnostic error.

### 4.7 ARM Instruction Encoding

**REQ-AS-070** *(State-driven)*
While in ARM state (`.arm`), the assembler shall emit 32-bit ARM instruction encodings with condition code fields.

**REQ-AS-071** *(State-driven)*
While in Thumb state (`.thumb`), the assembler shall emit 16-bit narrow Thumb or 32-bit wide Thumb-2 encodings, selecting the narrowest valid encoding by default.

**REQ-AS-072** *(Event-driven)*
When a `.w` suffix is present on a Thumb instruction, the assembler shall force the wide (32-bit) encoding.

**REQ-AS-073** *(Event-driven)*
When a `.n` suffix is present on a Thumb instruction, the assembler shall force the narrow (16-bit) encoding or emit an error if the narrow form is not available.

**REQ-AS-074** *(Ubiquitous)*
The assembler shall encode barrel shifter operands (LSL, LSR, ASR, ROR, RRX) with both immediate and register shift amounts.

**REQ-AS-075** *(Ubiquitous)*
The assembler shall encode IT blocks for Thumb-2 with up to four conditionally-executed instructions.

**REQ-AS-076** *(Ubiquitous)*
The assembler shall encode all VFPv3/v4 floating-point and NEON Advanced SIMD instructions with correct coprocessor and element size fields.

### 4.8 AArch64 Instruction Encoding

**REQ-AS-080** *(Ubiquitous)*
The assembler shall emit fixed-width 32-bit A64 instruction encodings for all AArch64 instructions.

**REQ-AS-081** *(Ubiquitous)*
The assembler shall encode logical immediates using the AArch64 bitmask immediate format (N:immr:imms fields).

**REQ-AS-082** *(Unwanted behavior)*
If a logical immediate value cannot be represented in the AArch64 bitmask format, then the assembler shall emit a diagnostic error.

**REQ-AS-083** *(Ubiquitous)*
The assembler shall encode ARMv8.1 LSE atomic instructions (`LDADD`, `LDCLR`, `LDEOR`, `LDSET`, `SWP`, `CAS`, `CASP` and all ordering/size variants).

**REQ-AS-084** *(Ubiquitous)*
The assembler shall encode ARMv8.1 RDMA instructions (`SQRDMLAH`, `SQRDMLSH`).

### 4.9 Relaxation

**REQ-AS-090** *(Event-driven)*
When a branch target is not resolvable in the first pass, the assembler shall assume the shortest encoding and re-evaluate in subsequent passes.

**REQ-AS-091** *(Event-driven)*
When an x86 `jmp rel8` or `jcc rel8` target is out of ±127 byte range, the assembler shall promote the encoding to `jmp rel32` or `jcc rel32`.

**REQ-AS-092** *(Event-driven)*
When an ARM B/BL target is out of ±32MB range, the assembler shall emit a diagnostic error (no veneer insertion at assembler level).

**REQ-AS-093** *(Event-driven)*
When a Thumb B.W/BL target is out of ±16MB range, the assembler shall emit a diagnostic error.

**REQ-AS-094** *(Ubiquitous)*
The assembler shall iterate relaxation passes until all instruction sizes stabilize or a maximum iteration count is reached.

**REQ-AS-095** *(Unwanted behavior)*
If relaxation does not converge within 100 iterations, then the assembler shall emit a diagnostic error and abort.

### 4.10 ELF Output

**REQ-AS-100** *(Ubiquitous)*
The assembler shall produce ET_REL ELF object files via `libelfobj`.

**REQ-AS-101** *(Ubiquitous)*
The assembler shall emit ELF32 for i386 and ARMv7 targets, and ELF64 for x86-64 and AArch64 targets.

**REQ-AS-102** *(Ubiquitous)*
The assembler shall emit `.symtab`, `.strtab`, and `.shstrtab` sections in every output.

**REQ-AS-103** *(State-driven)*
While targeting i386 or ARMv7, the assembler shall emit REL-format relocations (addend encoded in instruction).

**REQ-AS-104** *(State-driven)*
While targeting x86-64 or AArch64, the assembler shall emit RELA-format relocations (explicit addend field).

**REQ-AS-105** *(Event-driven)*
When the source contains `.cfi_*` directives, the assembler shall emit `.eh_frame` and optionally `.eh_frame_hdr` sections with correct CIE/FDE records.

**REQ-AS-106** *(Event-driven)*
When the source contains `.file` and `.loc` directives, the assembler shall emit `.debug_line` DWARF sections.

**REQ-AS-107** *(Event-driven)*
When targeting x86-64-v2 or higher, the assembler shall emit a `.note.gnu.property` section with `GNU_PROPERTY_X86_ISA_1_NEEDED` marking the highest ISA level used.

**REQ-AS-108** *(Ubiquitous)*
The assembler shall emit a `.note.GNU-stack` section to indicate non-executable stack by default.

**REQ-AS-109** *(State-driven)*
While targeting ARMv7, the assembler shall set `e_flags` with `EF_ARM_ABI_VER5` and the appropriate float ABI flag.

**REQ-AS-110** *(State-driven)*
While targeting ARMv7, the assembler shall emit ARM mapping symbols (`$a`, `$t`, `$d`) at ARM/Thumb/data transitions.

**REQ-AS-111** *(State-driven)*
While targeting AArch64, the assembler shall emit mapping symbols (`$x`, `$d`) at code/data transitions.

### 4.11 Diagnostics

**REQ-AS-120** *(Ubiquitous)*
The assembler shall format error diagnostics as `filename:line: error: message`.

**REQ-AS-121** *(Ubiquitous)*
The assembler shall format warning diagnostics as `filename:line: warning: message`.

**REQ-AS-122** *(Event-driven)*
When `--fatal-warnings` is specified, the assembler shall treat all warnings as errors.

**REQ-AS-123** *(Event-driven)*
When `--no-warn` is specified, the assembler shall suppress all warning diagnostics.

**REQ-AS-124** *(Ubiquitous)*
The assembler shall exit with code 0 on success, code 1 on any error.

**REQ-AS-125** *(Unwanted behavior)*
If the output file cannot be written (permission denied, disk full), then the assembler shall emit a diagnostic error and exit with code 1 without leaving a partial output file.

**REQ-AS-126** *(Ubiquitous)*
The assembler shall report the total number of errors and warnings at the end of assembly if any were emitted.

### 4.12 CLI Interface

**REQ-AS-130** *(Ubiquitous)*
The assembler shall accept `-o output.o` to specify the output file path (default: `a.out`).

**REQ-AS-131** *(Ubiquitous)*
The assembler shall accept `-march=ARCH` to select the target architecture and ISA level.

**REQ-AS-132** *(Ubiquitous)*
The assembler shall accept `-g` to include debugging information in the output.

**REQ-AS-133** *(Ubiquitous)*
The assembler shall accept `-al` to produce an assembly listing to standard output.

**REQ-AS-134** *(Ubiquitous)*
The assembler shall accept `--defsym sym=value` to pre-define a symbol with the given integer value.

**REQ-AS-135** *(Ubiquitous)*
The assembler shall accept `-D sym=value` to pre-define a symbol for conditional assembly.

**REQ-AS-136** *(Ubiquitous)*
The assembler shall accept `--statistics` to print memory usage and timing information to standard error.

**REQ-AS-137** *(Ubiquitous)*
The assembler shall accept `-W`/`--warn` and `--no-warn` to control warning output.

**REQ-AS-138** *(Ubiquitous)*
The assembler shall accept `-v`/`--version` to print the assembler version and exit.

**REQ-AS-139** *(Ubiquitous)*
The assembler shall accept `--help` and `--target-help` to print usage and per-architecture help respectively.

---

## 5. Non-Functional Requirements

**REQ-AS-200** *(Ubiquitous)*
The assembler shall produce deterministic output: identical source with identical options shall produce byte-identical object files.

**REQ-AS-201** *(Ubiquitous)*
The assembler shall not depend on any external assembler, compiler, or linker at runtime.

**REQ-AS-202** *(Ubiquitous)*
The assembler shall not invoke any child processes during assembly.

**REQ-AS-203** *(Ubiquitous)*
The assembler shall handle source files of at least 10 million lines without crashing.

**REQ-AS-204** *(Ubiquitous)*
The assembler shall handle symbol tables of at least 1 million symbols without crashing.

**REQ-AS-205** *(Ubiquitous)*
The assembler shall exit cleanly on out-of-memory conditions with a diagnostic message.

**REQ-AS-206** *(Ubiquitous)*
The assembler shall produce output parseable by the Substrate linker (`ld`), GNU `ld`, and LLVM `lld`.

**REQ-AS-207** *(Ubiquitous)*
The assembler shall be buildable as a host tool (`NATIVE_BUILD=1`) on Linux and as a Substrate native binary.

**REQ-AS-208** *(Ubiquitous)*
The assembler shall not execute arbitrary code from the source input at assembly time.

**REQ-AS-209** *(Ubiquitous)*
The assembler shall be crash-free on any input, including malformed, truncated, or adversarial source files.

---

## 6. User Stories

### US-01: Kernel Developer Assembling Boot Code

> As a **kernel developer**, I want to assemble i386 boot code written in AT&T syntax so that I can produce object files linkable with my kernel build.

**REQ-US-01-A** *(Event-driven)*
When the user invokes `as --32 -o boot.o boot.S`, the assembler shall produce an ELF32 EM_386 relocatable object.

**REQ-US-01-B** *(Ubiquitous)*
The assembler shall encode all i386 instructions including privileged instructions (`LGDT`, `LIDT`, `MOV CR0`, `WRMSR`, `HLT`, `CLI`, `STI`, `INVLPG`).

**REQ-US-01-C** *(Event-driven)*
When the source uses `.code16` and `.code32` directives, the assembler shall switch between 16-bit and 32-bit encoding modes within the same source file.

### US-02: Application Developer Using SSE4/AVX

> As an **application developer**, I want to write SIMD code using SSE4.2 and AVX2 intrinsics in inline assembly so that I can optimize hot loops.

**REQ-US-02-A** *(Event-driven)*
When `-march=x86-64-v2` is specified, the assembler shall accept all SSE3, SSSE3, SSE4.1, SSE4.2, and POPCNT instructions.

**REQ-US-02-B** *(Event-driven)*
When `-march=x86-64-v3` is specified, the assembler shall additionally accept all AVX, AVX2, FMA, BMI1, BMI2, F16C, LZCNT, and MOVBE instructions.

**REQ-US-02-C** *(Unwanted behavior)*
If the user writes an AVX-512 instruction while `-march=x86-64-v3` is active, then the assembler shall emit an error stating that the instruction requires x86-64-v4.

### US-03: Embedded Developer Targeting ARMv7

> As an **embedded developer**, I want to assemble ARMv7-A code with Thumb-2 and NEON instructions so that I can build firmware for a Cortex-A class processor.

**REQ-US-03-A** *(Event-driven)*
When `-march=armv7-a` is specified, the assembler shall accept ARM state, Thumb state, Thumb-2, VFPv3, and NEON instructions.

**REQ-US-03-B** *(State-driven)*
While in `.thumb` mode, the assembler shall select narrow 16-bit encodings where the operands allow and wide 32-bit encodings otherwise.

**REQ-US-03-C** *(Ubiquitous)*
The assembler shall correctly encode IT blocks with up to four conditioned Thumb instructions.

**REQ-US-03-D** *(Event-driven)*
When the source uses `LDR Rn, =constant` pseudo-instructions, the assembler shall generate a literal pool entry and a PC-relative load.

### US-04: Systems Programmer Targeting AArch64

> As a **systems programmer**, I want to assemble AArch64 code using ARMv8.1 atomic instructions so that I can implement lock-free data structures.

**REQ-US-04-A** *(Event-driven)*
When `-march=armv8.1-a` is specified, the assembler shall accept all ARMv8.0 and ARMv8.1 instructions including LSE atomics (`LDADD`, `SWP`, `CAS`, `CASP` and all ordering/size variants).

**REQ-US-04-B** *(Ubiquitous)*
The assembler shall encode AArch64 system instructions (`MSR`, `MRS`, `DC`, `IC`, `AT`, `TLBI`) with the correct system register encoding.

**REQ-US-04-C** *(Ubiquitous)*
The assembler shall encode Advanced SIMD instructions with correct element size specifiers (e.g., `FADD V0.4S, V1.4S, V2.4S`).

### US-05: Build System Integration

> As a **build system maintainer**, I want `as` to be a drop-in replacement for GNU `as` so that existing Makefiles and build scripts work without modification.

**REQ-US-05-A** *(Ubiquitous)*
The assembler shall accept all command-line options documented in this specification with semantics compatible with GNU `as`.

**REQ-US-05-B** *(Event-driven)*
When an unrecognized option is encountered, the assembler shall emit a warning and continue rather than aborting (for forward-compatibility with new GAS options).

**REQ-US-05-C** *(Ubiquitous)*
The assembler shall produce ELF output with section names, symbol names, and relocation types identical to those produced by GNU `as` for the same input.

### US-06: Compiler Backend Developer

> As a **compiler backend developer**, I want to programmatically generate assembly and have the assembler handle forward references, relaxation, and `.cfi` directives so that I can focus on code generation.

**REQ-US-06-A** *(Ubiquitous)*
The assembler shall handle forward references to labels without requiring a separate declaration pass.

**REQ-US-06-B** *(Ubiquitous)*
The assembler shall automatically relax short branches to long branches when the target is out of range.

**REQ-US-06-C** *(Event-driven)*
When `.cfi_startproc` and `.cfi_endproc` bracket a function, the assembler shall emit a complete FDE record in `.eh_frame`.

**REQ-US-06-D** *(Ubiquitous)*
The assembler shall process `.file` and `.loc` directives to emit DWARF `.debug_line` information usable by `addr2line` and debuggers.

---

## 7. Developer Stories

### DS-01: Adding a New Instruction

> As an **assembler developer**, I want the instruction encoding to be table-driven so that adding a new instruction requires only a table entry, not new code.

**REQ-DS-01-A** *(Ubiquitous)*
The assembler shall define instruction encodings in declarative tables indexed by mnemonic, operand pattern, and architecture.

**REQ-DS-01-B** *(Ubiquitous)*
The assembler shall derive the encoding (opcode bytes, ModR/M, prefix selection, VEX/EVEX fields) from the table entry and operand values at assembly time.

**REQ-DS-01-C** *(Ubiquitous)*
The assembler shall validate that all table entries produce correct output via automated encoding tests.

### DS-02: Adding a New Architecture

> As an **assembler developer**, I want architecture-specific code to be isolated behind a clean backend interface so that adding a new architecture does not require modifying the core assembler.

**REQ-DS-02-A** *(Ubiquitous)*
The assembler shall define an architecture backend interface that provides: instruction parsing, encoding, relocation emission, relaxation, and register name resolution.

**REQ-DS-02-B** *(Ubiquitous)*
The assembler shall select the backend at startup based on the `-march` and `--32`/`--64` options.

**REQ-DS-02-C** *(Ubiquitous)*
The assembler shall share the lexer, directive handling, symbol table, section management, and ELF output across all backends.

### DS-03: Testing an Instruction Encoding

> As an **assembler developer**, I want to write a test that specifies an instruction string and the expected machine code bytes so that regressions are caught automatically.

**REQ-DS-03-A** *(Ubiquitous)*
The assembler test framework shall support test cases in the format: `{ input: "movl %eax, %ebx", arch: "i386", expected: [0x89, 0xC3] }`.

**REQ-DS-03-B** *(Ubiquitous)*
The assembler test framework shall compare the assembled output byte-by-byte against the expected encoding and report mismatches with both expected and actual bytes.

**REQ-DS-03-C** *(Ubiquitous)*
The assembler test suite shall contain at least one encoding test for every instruction mnemonic in the instruction tables.

### DS-04: Debugging Relaxation Issues

> As an **assembler developer**, I want the relaxation engine to log its decisions so that I can diagnose convergence failures and incorrect encoding selections.

**REQ-DS-04-A** *(Event-driven)*
When `--statistics` is specified, the assembler shall print the number of relaxation passes performed and the number of instructions that changed size.

**REQ-DS-04-B** *(Event-driven)*
When an internal debug flag is set, the assembler shall log each relaxation decision (instruction, old size, new size, target distance) to standard error.

### DS-05: Maintaining ELF Compatibility

> As an **assembler developer**, I want automated compatibility tests that compare our output against GNU `as` output so that interoperability regressions are caught.

**REQ-DS-05-A** *(Ubiquitous)*
The assembler CI shall include tests that assemble a corpus of source files with both Substrate `as` and GNU `as`, then compare the resulting ELF structure using `readelf -a`.

**REQ-DS-05-B** *(Ubiquitous)*
The assembler CI shall include tests that link Substrate `as` output with GNU `ld` and verify the resulting executable runs correctly.

**REQ-DS-05-C** *(Ubiquitous)*
The assembler CI shall include tests that link GNU `as` output with Substrate `ld` and verify the resulting executable runs correctly.

### DS-06: Fuzzing the Parser

> As an **assembler developer**, I want a fuzz harness for the parser so that crashes on malformed input are discovered early.

**REQ-DS-06-A** *(Ubiquitous)*
The assembler shall provide a fuzz harness entry point that accepts arbitrary byte buffers as assembly source.

**REQ-DS-06-B** *(Ubiquitous)*
The fuzz harness shall exercise the full pipeline: lexer → parser → encoder → output, for each architecture backend.

**REQ-DS-06-C** *(Ubiquitous)*
The assembler shall be crash-free for all inputs discovered by fuzzing (zero ASAN/UBSAN findings).

---

## 8. Traceability Matrix

| Requirement   | User Story | Developer Story | Tasklist Section |
|---------------|------------|-----------------|------------------|
| REQ-AS-010–014 | US-05     |                 | §1a (Lexer)      |
| REQ-AS-020–025 | US-06     | DS-01           | §1a (Lexer)      |
| REQ-AS-030–036 | US-01, US-02 | DS-01        | §1b (Parser)     |
| REQ-AS-040–048 | US-05, US-06 |              | §1d–1f (Directives) |
| REQ-AS-050–057 | US-02, US-03, US-04 | DS-02 | §1h (CLI)        |
| REQ-AS-060–065 | US-01, US-02 | DS-01, DS-03 | §2–5 (x86)       |
| REQ-AS-070–076 | US-03     | DS-01, DS-02    | §6 (ARMv7)       |
| REQ-AS-080–084 | US-04     | DS-01, DS-02    | §7 (AArch64)     |
| REQ-AS-090–095 | US-06     | DS-04           | §1g (Relaxation)  |
| REQ-AS-100–111 | US-01–US-06 | DS-05         | §1f (ELF Output)  |
| REQ-AS-120–126 | US-05     | DS-04           | §1h (CLI)        |
| REQ-AS-130–139 | US-05     |                 | §1h (CLI)        |
| REQ-AS-200–209 | US-05     | DS-05, DS-06    | §8–10 (Testing)   |

---

## 9. Acceptance Criteria

1. `as --32 -o test.o test.s` produces a valid ELF32 EM_386 object linkable by Substrate `ld` and GNU `ld`.
2. `as --64 -march=x86-64-v4 -o test.o test.s` accepts all AVX-512 instructions and produces a valid ELF64 object with `.note.gnu.property`.
3. `as -march=armv7-a -o test.o test.s` produces a valid ELF32 EM_ARM object with correct `e_flags`, mapping symbols, and REL relocations.
4. `as -march=armv8.1-a -o test.o test.s` produces a valid ELF64 EM_AARCH64 object with RELA relocations and correctly encoded LSE atomics.
5. Assembly listing (`-al`) output matches instruction encoding in the object file.
6. Two identical assembly runs produce byte-identical output.
7. The fuzz harness runs for 24 hours without discovering any crashes.
8. `readelf -a` structural validation passes on all outputs.
9. Full toolchain path (`as` → `ld` → execution) works for all four architectures.

---

## Appendix A: Legacy Wrapper Phase Notes (Merged)

The prior phase-1 implementation used delegated backend assembly (`gcc -c -x assembler-with-cpp`) with post-assembly ELF validation. These notes remain relevant for compatibility and migration planning during standalone refactor:

- Preserve command-line compatibility (`-32/-64/-I/-D/-Wa/-march/-mtune/-g`) while native parsing/encoding is phased in.
- Preserve deterministic object output and stable diagnostics as non-regression gates.
- Maintain `ld` and `cc` pipeline interoperability as release gates throughout the transition.
- Keep compatibility/documented-incompatibility behavior explicit in docs and regression tests.

## Appendix B: Per-Architecture Reference and GNU Property Semantics

- The per-architecture instruction-family appendix is maintained in `docs/specs/as_arch_reference.md`.
- x86-64 ISA note semantics are tracked through `.note.gnu.property` with:
  - Property type: `GNU_PROPERTY_X86_ISA_1_NEEDED` (`0xc0008002`).
  - ISA bits: `GNU_PROPERTY_X86_ISA_1_BASELINE`, `GNU_PROPERTY_X86_ISA_1_V2`, `GNU_PROPERTY_X86_ISA_1_V3`, `GNU_PROPERTY_X86_ISA_1_V4`.
- The note bitmask communicates minimum required ISA level for the object and is used by loader/tooling compatibility checks.
