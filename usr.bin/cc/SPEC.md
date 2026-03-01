# Substrate C Compiler (`cc`) — Specification

## 1. Purpose

The Substrate C compiler (`cc`) is a standalone, multi-target C compiler that translates C source files into native assembly or object files. It replaces the `--bootstrap-gcc` fallback with a fully native implementation that has no external compiler dependencies at runtime. The compiler targets ISO C23 conformance with GNU and Clang extension compatibility sufficient to build real-world software (Bash, coreutils, the Linux kernel).

## 2. Scope

| Attribute         | Value                                                         |
|-------------------|---------------------------------------------------------------|
| Binary name       | `cc` (also `cpp` for preprocessor-only mode)                  |
| Install path      | `/usr/bin/cc`, `/usr/bin/cpp`                                 |
| Input             | C source (`.c`, `.h`), preprocessed C (`.i`), assembly (`.s`) |
| Output            | Assembly (`.s`), object files (`.o`) via system `as`, executables via system `ld` |
| C standards       | C99, C11, C17, C23 (`-std=c99/c11/c17/c23` and GNU dialects) |
| Target ABIs       | i386 SysV cdecl (`-m32`), x86-64 SysV AMD64 (`-m64`)         |
| Library deps      | None at runtime (self-contained; delegates to `as` and `ld`)  |
| Host build        | `NATIVE_BUILD=1` for development/test on Linux/BSD host       |

## 3. Definitions

| Term               | Definition                                                                    |
|--------------------|-------------------------------------------------------------------------------|
| EARS               | Easy Approach to Requirements Syntax (ISO/IEC/IEEE 29148 compatible)          |
| Translation unit   | A source file after preprocessing, as defined by ISO C §5.1.1.1              |
| AST                | Abstract Syntax Tree — internal representation of parsed C source             |
| SSA                | Static Single Assignment — IR form used by the middle-end optimizer           |
| Sema               | Semantic analysis — type checking, constraint validation, diagnostic emission |
| Lowering           | Translation from AST to SSA IR (middle-end) or from IR to assembly (backend) |
| ABI                | Application Binary Interface — calling conventions, type sizes, alignment     |
| Aggregate          | Struct, union, or array type passed or returned by value                      |
| Personality        | Substrate-specific term for OS-level ABI compatibility layer                  |
| Declaration specifier | Storage class, type specifier, or qualifier within a declaration           |
| VLA                | Variable Length Array — array with runtime-determined size (C99)              |

---

## 4. Functional Requirements

### 4.1 Driver and CLI

**REQ-CC-010** *(Ubiquitous)*
The compiler shall accept one or more C source files, preprocessed files, assembly files, or object files as positional arguments.

**REQ-CC-011** *(Event-driven)*
When `-E` is specified, the compiler shall preprocess the input and write the result to standard output (or `-o` file), without compiling.

**REQ-CC-012** *(Event-driven)*
When `-S` is specified, the compiler shall compile C source to assembly output (`.s`), without assembling.

**REQ-CC-013** *(Event-driven)*
When `-c` is specified, the compiler shall compile and assemble to produce a relocatable object file (`.o`), without linking.

**REQ-CC-014** *(Ubiquitous)*
When no stage-limiting flag is given, the compiler shall preprocess, compile, assemble, and link to produce an executable.

**REQ-CC-015** *(Ubiquitous)*
The compiler shall accept `-o path` to specify the output file path.

**REQ-CC-016** *(Ubiquitous)*
The compiler shall accept `-std=MODE` where MODE is one of: `c99`, `gnu99`, `c11`, `gnu11`, `c17`, `gnu17`, `c23`, `gnu23`.

**REQ-CC-017** *(Ubiquitous)*
The compiler shall accept `-m32` and `-m64` to select the i386 and x86-64 targets respectively.

**REQ-CC-018** *(Ubiquitous)*
The compiler shall accept optimization level flags: `-O0`, `-O1`, `-O2`, `-O3`, `-Os`.

**REQ-CC-019** *(Ubiquitous)*
The compiler shall accept `-g` to emit debug information (DWARF `.file`/`.loc`/`.cfi_*` directives).

**REQ-CC-020** *(Ubiquitous)*
The compiler shall accept `-Wall`, `-Wextra`, `-Werror`, `-Wpedantic`, and `-pedantic-errors`.

**REQ-CC-021** *(Ubiquitous)*
The compiler shall accept `-I dir`, `-D name[=value]`, `-U name`, `-include file`, and `-imacros file`.

**REQ-CC-022** *(Ubiquitous)*
The compiler shall accept `-fPIC`, `-shared`, `-pthread`, and pass them to the assembler/linker.

**REQ-CC-023** *(Event-driven)*
When `-v` is specified, the compiler shall print the commands it executes for each stage.

**REQ-CC-024** *(Event-driven)*
When `-###` is specified, the compiler shall print the commands it would execute without running them.

**REQ-CC-025** *(Event-driven)*
When `--bootstrap-gcc` is specified, the compiler shall delegate to the system GCC for compilation.

**REQ-CC-026** *(Event-driven)*
When `-emit-ssa` is specified with a `.ir` input, the compiler shall verify the SSA via `ir-verifier`.

### 4.2 Preprocessor

**REQ-CC-030** *(Ubiquitous)*
The preprocessor shall implement all C99 directives: `#define`, `#undef`, `#include`, `#if`, `#ifdef`, `#ifndef`, `#elif`, `#else`, `#endif`, `#line`, `#error`, `#pragma`.

**REQ-CC-031** *(Ubiquitous)*
The preprocessor shall implement function-like macros with `__VA_ARGS__`, correct argument prescan, stringification (`#`), token pasting (`##`), and recursion prevention.

**REQ-CC-032** *(Ubiquitous)*
The preprocessor shall implement C23 directives: `#elifdef`, `#elifndef`, `#warning`, `#embed`.

**REQ-CC-033** *(Ubiquitous)*
The preprocessor shall implement `__VA_OPT__` with version gating (C23 and GNU modes only).

**REQ-CC-034** *(Ubiquitous)*
The preprocessor shall implement probing macros: `__has_include`, `__has_embed`, `__has_c_attribute`.

**REQ-CC-035** *(Ubiquitous)*
The preprocessor shall implement GNU extensions: `#include_next`, `,##__VA_ARGS__`, named variadic macros, `#pragma GCC push_macro`/`pop_macro`.

**REQ-CC-036** *(Ubiquitous)*
The preprocessor shall define version macros: `__COUNTER__`, `__BASE_FILE__`, `__FILE_NAME__`, `__INCLUDE_LEVEL__`, `__TIMESTAMP__`.

**REQ-CC-037** *(Ubiquitous)*
The preprocessor shall implement Clang feature probing: `__has_feature`, `__has_extension`, `__has_builtin`, `__has_attribute`, `__has_warning`, `__has_declspec_attribute`, `__is_identifier`.

**REQ-CC-038** *(Ubiquitous)*
The preprocessor shall implement dependency generation: `-M`, `-MM`, `-MD`, `-MMD`, `-MF`, `-MT`, `-MQ`.

**REQ-CC-039** *(Ubiquitous)*
The preprocessor shall emit `file:line:col` diagnostics with include stack traces and macro expansion context.

### 4.3 Type System

**REQ-CC-050** *(Ubiquitous)*
The compiler shall represent all C23 scalar types: `_Bool`, `char`, `signed char`, `unsigned char`, `short`, `unsigned short`, `int`, `unsigned int`, `long`, `unsigned long`, `long long`, `unsigned long long`, `float`, `double`, `long double`.

**REQ-CC-051** *(Ubiquitous)*
The compiler shall represent `void` as a type for return types, pointer bases, and incomplete type contexts.

**REQ-CC-052** *(Ubiquitous)*
The compiler shall represent pointer types to any base type with arbitrary nesting depth.

**REQ-CC-053** *(Ubiquitous)*
The compiler shall represent struct, union, and enum types as distinct tracked types with tag identity.

**REQ-CC-054** *(Ubiquitous)*
The compiler shall represent array types with element type, dimension count, and per-dimension size.

**REQ-CC-055** *(Ubiquitous)*
The compiler shall represent function types with return type, parameter types, and variadic flag.

**REQ-CC-056** *(Event-driven)*
When `-std=c11` or newer is active, the compiler shall represent `_Atomic(T)` as a qualified variant of type `T`.

**REQ-CC-057** *(Event-driven)*
When `-std=c99` or newer is active, the compiler shall represent `_Complex` and `_Imaginary` type variants.

**REQ-CC-058** *(Event-driven)*
When `-std=c23` or newer is active, the compiler shall represent `_BitInt(N)` types with tracked width `N`.

**REQ-CC-059** *(Event-driven)*
When `-std=c23` or newer is active, the compiler shall represent `_Decimal32`, `_Decimal64`, and `_Decimal128` types.

**REQ-CC-060** *(Ubiquitous)*
The compiler shall compute `sizeof`, `_Alignof`, and alignment for all types according to the selected target ABI.

### 4.4 Lexical Analysis

**REQ-CC-070** *(Ubiquitous)*
The lexer shall tokenize C source into identifiers, keywords, integer constants, floating constants, character constants, string literals, punctuators, and preprocessing tokens.

**REQ-CC-071** *(Ubiquitous)*
The lexer shall recognize integer literals in decimal, octal (`0` prefix), hexadecimal (`0x`/`0X` prefix), and binary (`0b`/`0B` prefix, C23) forms with suffix typing (`u`, `l`, `ll`, `ull`, etc.).

**REQ-CC-072** *(Ubiquitous)*
The lexer shall recognize floating-point literals in decimal and hexadecimal forms with suffix typing (`f`, `l`).

**REQ-CC-073** *(Event-driven)*
When `-std=c23` or newer is active, the lexer shall accept digit separators (`'`) in numeric constants.

**REQ-CC-074** *(Ubiquitous)*
The lexer shall process trigraph sequences (in modes where enabled) and digraph tokens.

**REQ-CC-075** *(Ubiquitous)*
The lexer shall process line splicing (escaped newlines) and universal character names (`\uNNNN`, `\UNNNNNNNN`).

**REQ-CC-076** *(Ubiquitous)*
The lexer shall tokenize all C23 keywords, including `alignas`, `alignof`, `bool`, `constexpr`, `false`, `nullptr`, `static_assert`, `thread_local`, `true`, `typeof`, `typeof_unqual`.

### 4.5 Parsing

**REQ-CC-080** *(Ubiquitous)*
The parser shall implement the full C23 expression grammar: primary, postfix, unary, cast, multiplicative, additive, shift, relational, equality, bitwise, logical, conditional, assignment, and comma expressions.

**REQ-CC-081** *(Ubiquitous)*
The parser shall implement the full C23 declaration grammar: declaration specifiers, declarators (including pointer, array, function, and nested declarators), initializers (including designated initializers), and type names.

**REQ-CC-082** *(Ubiquitous)*
The parser shall implement the full C23 statement grammar: labeled, compound, expression, selection (`if`/`else`, `switch`/`case`/`default`), iteration (`while`, `do`, `for`), jump (`goto`, `continue`, `break`, `return`), and null statements.

**REQ-CC-083** *(Ubiquitous)*
The parser shall implement struct and union definitions with member declarations, including bitfield widths.

**REQ-CC-084** *(Ubiquitous)*
The parser shall implement enum definitions with optional explicit underlying type (C23).

**REQ-CC-085** *(Ubiquitous)*
The parser shall implement `typedef` declarations in all positions (file scope, block scope, function parameters).

**REQ-CC-086** *(Ubiquitous)*
The parser shall implement `_Generic` selection expressions with type-association lists.

**REQ-CC-087** *(Ubiquitous)*
The parser shall implement `_Static_assert` with one or two arguments.

**REQ-CC-088** *(Ubiquitous)*
The parser shall implement compound literals with type names and brace-enclosed initializers.

**REQ-CC-089** *(Event-driven)*
When `-std=c23` or newer is active, the parser shall accept `[[attribute]]` syntax for standard attributes.

**REQ-CC-090** *(Ubiquitous)*
The parser shall implement GNU `__attribute__((...))` syntax with nested attribute lists.

**REQ-CC-091** *(Ubiquitous)*
The parser shall implement GNU inline assembly: basic `asm("...")`, extended `asm` with inputs/outputs/clobbers, named operands, `asm volatile`, and `asm goto` with label lists.

### 4.6 Semantic Analysis

**REQ-CC-100** *(Ubiquitous)*
The semantic analyzer shall enforce integer promotions, usual arithmetic conversions, and implicit conversion rules per ISO C.

**REQ-CC-101** *(Ubiquitous)*
The semantic analyzer shall enforce lvalue/rvalue constraints, modifiable-lvalue requirements, and array/function-to-pointer decay.

**REQ-CC-102** *(Ubiquitous)*
The semantic analyzer shall enforce type compatibility for function declarations, definitions, and calls, including prototype conformance and default argument promotions.

**REQ-CC-103** *(Ubiquitous)*
The semantic analyzer shall enforce storage-class and qualifier constraints: conflicting specifiers, duplicate qualifiers, `restrict` on non-pointer types.

**REQ-CC-104** *(Ubiquitous)*
The semantic analyzer shall validate struct/union member access (`.` and `->`) with correct type resolution through nested member chains.

**REQ-CC-105** *(Ubiquitous)*
The semantic analyzer shall validate bitfield width constraints: width ≤ type width, zero-width only for unnamed, signed/unsigned integer types only.

**REQ-CC-106** *(Ubiquitous)*
The semantic analyzer shall validate `goto` targets exist within the enclosing function and diagnose jumping into VLA scope.

**REQ-CC-107** *(Ubiquitous)*
The semantic analyzer shall validate `switch`/`case` for duplicate case values, case ranges, and `default` multiplicity.

**REQ-CC-108** *(Ubiquitous)*
The semantic analyzer shall validate inline assembly constraints, operand types, clobber lists, and template references.

**REQ-CC-109** *(Ubiquitous)*
The semantic analyzer shall validate GNU `__sync_*` and `__atomic_*` builtin argument counts, types, and memory-order values.

**REQ-CC-110** *(Ubiquitous)*
The semantic analyzer shall validate `_Generic` for duplicate type associations and ensure at most one `default` association.

### 4.7 AST-to-IR Lowering (Middle-End)

**REQ-CC-120** *(Ubiquitous)*
The middle-end shall lower all C expressions to SSA-form temporaries with explicit type widths.

**REQ-CC-121** *(Ubiquitous)*
The middle-end shall lower struct/union member access to base-pointer + offset load/store operations.

**REQ-CC-122** *(Ubiquitous)*
The middle-end shall lower bitfield access to load-mask-shift (read) and load-mask-or-store (write) sequences.

**REQ-CC-123** *(Ubiquitous)*
The middle-end shall lower aggregate pass-by-value into ABI-conformant register/stack sequences per the SysV ABI specification.

**REQ-CC-124** *(Ubiquitous)*
The middle-end shall lower aggregate return-by-value into hidden-pointer or register-return sequences per the SysV ABI specification.

**REQ-CC-125** *(Ubiquitous)*
The middle-end shall lower `switch`/`case` into branch trees or jump tables depending on case density.

**REQ-CC-126** *(Ubiquitous)*
The middle-end shall lower GNU statement expressions `({ ... })` with correct value forwarding from the final expression.

**REQ-CC-127** *(Ubiquitous)*
The middle-end shall lower `_Generic` to the selected association's expression at compile time.

**REQ-CC-128** *(Ubiquitous)*
The middle-end shall lower `__sync_*` and `__atomic_*` builtins to atomic RMW instructions or `lock`-prefixed sequences.

**REQ-CC-129** *(Ubiquitous)*
The middle-end shall lower `asm goto` with CFG edges from the asm block to each named C label.

**REQ-CC-130** *(Event-driven)*
When `-O1` or higher is specified, the middle-end shall perform constant folding and dead temporary elimination.

### 4.8 Code Generation (Backend)

**REQ-CC-140** *(Ubiquitous)*
The backend shall emit GAS-compatible x86-64 AT&T syntax assembly when `-m64` is active.

**REQ-CC-141** *(Ubiquitous)*
The backend shall emit GAS-compatible i386 AT&T syntax assembly when `-m32` is active.

**REQ-CC-142** *(Ubiquitous)*
The backend shall implement x86-64 SysV AMD64 ABI: integer args in `%rdi`, `%rsi`, `%rdx`, `%rcx`, `%r8`, `%r9`; SSE args in `%xmm0`–`%xmm7`; stack overflow args; return in `%rax`/`%rdx` or `%xmm0`/`%xmm1`.

**REQ-CC-143** *(Ubiquitous)*
The backend shall implement i386 SysV cdecl ABI: all args on stack right-to-left; return in `%eax`/`%edx` or `%st(0)`.

**REQ-CC-144** *(Ubiquitous)*
The backend shall emit correct aggregate (struct/union) classification and passing per x86-64 SysV ABI §3.2.3 (INTEGER, SSE, MEMORY classes).

**REQ-CC-145** *(Ubiquitous)*
The backend shall emit correct aggregate return handling: hidden first parameter for MEMORY class, register pairs for INTEGER/SSE class.

**REQ-CC-146** *(Ubiquitous)*
The backend shall emit `long double` operations using x87 `%st` registers and correct 80-bit or 96-bit stack alignment.

**REQ-CC-147** *(Event-driven)*
When `-g` is specified, the backend shall emit `.file`, `.loc`, and `.cfi_*` directives for DWARF debug and unwind information.

**REQ-CC-148** *(Ubiquitous)*
The backend shall perform stack-slot compaction (linear-scan style) to minimize frame size.

**REQ-CC-149** *(Ubiquitous)*
The backend shall emit inline assembly templates with correct operand substitution, constraint satisfaction, and clobber preservation.

### 4.9 GNU Extension Support

**REQ-CC-160** *(Ubiquitous)*
The compiler shall implement statement expressions `({ ... })` with expression-value semantics.

**REQ-CC-161** *(Ubiquitous)*
The compiler shall implement labels-as-values (`&&label`) and computed goto (`goto *expr`).

**REQ-CC-162** *(Ubiquitous)*
The compiler shall implement locally declared labels (`__label__`).

**REQ-CC-163** *(Ubiquitous)*
The compiler shall implement nested functions with access to enclosing scope variables via trampolines.

**REQ-CC-164** *(Ubiquitous)*
The compiler shall implement `typeof`/`__typeof__` and `typeof_unqual`/`__typeof_unqual__`.

**REQ-CC-165** *(Ubiquitous)*
The compiler shall implement the omitted-middle ternary (`x ?: y`).

**REQ-CC-166** *(Ubiquitous)*
The compiler shall implement case ranges (`case low ... high:`).

**REQ-CC-167** *(Ubiquitous)*
The compiler shall implement cast-to-union, zero-length arrays, empty structs, and `void*` arithmetic.

**REQ-CC-168** *(Ubiquitous)*
The compiler shall implement all GNU `__attribute__` forms used in practice: function attributes (`noreturn`, `always_inline`, `noinline`, `hot`, `cold`, `format`, `nonnull`, `malloc`, `alias`, `weak`, `used`, `unused`, `flatten`, `target`, `visibility`), variable attributes (`aligned`, `packed`, `section`, `used`, `unused`, `tls_model`, `cleanup`, `visibility`), and type attributes (`aligned`, `packed`, `transparent_union`, `vector_size`, `may_alias`).

**REQ-CC-169** *(Ubiquitous)*
The compiler shall implement GNU builtins: `__builtin_expect`, `__builtin_constant_p`, `__builtin_clz`/`ctz`/`popcount`/`parity`/`ffs` families, overflow builtins, object-size builtins, `__builtin_types_compatible_p`, `__builtin_choose_expr`, `__builtin_offsetof`, `__builtin_unreachable`, `__builtin_trap`, and varargs builtins.

**REQ-CC-170** *(Ubiquitous)*
The compiler shall implement legacy `__sync_*` atomics (`fetch_and_add`, `fetch_and_sub`, `bool_compare_and_swap`, `lock_test_and_set`, `lock_release`, `synchronize`) with correct codegen.

**REQ-CC-171** *(Ubiquitous)*
The compiler shall implement C11/GNU `__atomic_*` builtins (`load_n`, `store_n`, `exchange_n`, `fetch_add`, `fetch_sub`, `compare_exchange`) with memory-order codegen.

### 4.10 C23 Standard Attribute Support

**REQ-CC-180** *(Event-driven)*
When `-std=c23` or newer is active, the compiler shall parse `[[attribute-list]]` syntax on declarations, statements, and labels.

**REQ-CC-181** *(Event-driven)*
When `-std=c23` or newer is active, the compiler shall implement: `[[deprecated]]`, `[[fallthrough]]`, `[[maybe_unused]]`, `[[nodiscard]]`, `[[noreturn]]`, `[[reproducible]]`, `[[unsequenced]]`.

**REQ-CC-182** *(Event-driven)*
When `__has_c_attribute(name)` is evaluated in a preprocessor conditional, the compiler shall return the attribute's standard version date or 0.

### 4.11 Clang Compatibility

**REQ-CC-190** *(Ubiquitous)*
The compiler shall accept `#pragma clang diagnostic push/pop/ignored/warning/error`.

**REQ-CC-191** *(Ubiquitous)*
The compiler shall accept `#pragma clang attribute push/pop`.

**REQ-CC-192** *(Ubiquitous)*
The compiler shall accept `#pragma clang loop` and `#pragma clang section` (parse and preserve).

**REQ-CC-193** *(Ubiquitous)*
The compiler shall accept Clang-specific builtin aliases when in GNU mode.

### 4.12 Diagnostics

**REQ-CC-200** *(Ubiquitous)*
The compiler shall format error diagnostics as `file:line:col: error: message`.

**REQ-CC-201** *(Ubiquitous)*
The compiler shall format warning diagnostics as `file:line:col: warning: message`.

**REQ-CC-202** *(Event-driven)*
When `-Werror` is active, the compiler shall treat all warnings as errors.

**REQ-CC-203** *(Event-driven)*
When `-Wpedantic` is active, the compiler shall diagnose extensions used outside the active standard.

**REQ-CC-204** *(Ubiquitous)*
The compiler shall exit with code 0 on success, code 1 on any error.

**REQ-CC-205** *(Ubiquitous)*
The compiler shall provide include-stack context and macro-expansion traces in diagnostic output.

---

## 5. Non-Functional Requirements

**REQ-CC-300** *(Ubiquitous)*
The compiler shall produce deterministic output: identical source with identical options shall produce byte-identical assembly.

**REQ-CC-301** *(Ubiquitous)*
The compiler shall not depend on any external compiler at runtime (except when `--bootstrap-gcc` is explicitly specified).

**REQ-CC-302** *(Ubiquitous)*
The compiler shall handle translation units of at least 500,000 lines without crashing.

**REQ-CC-303** *(Ubiquitous)*
The compiler shall exit cleanly on out-of-memory conditions with a diagnostic message.

**REQ-CC-304** *(Ubiquitous)*
The compiler shall produce assembly compatible with the Substrate assembler (`as`) and GNU `as`.

**REQ-CC-305** *(Ubiquitous)*
The compiler shall be buildable as a host tool (`NATIVE_BUILD=1`) on Linux and as a Substrate native binary.

**REQ-CC-306** *(Ubiquitous)*
The compiler shall compile cleanly with `-Wall -Wextra -Werror` on the host toolchain.

**REQ-CC-307** *(Ubiquitous)*
The compiler shall be crash-free on any input, including malformed, truncated, or adversarial C source files.

**REQ-CC-308** *(Ubiquitous)*
The compiler shall produce output that, when assembled and linked, yields executables with behavior identical to GCC/Clang-compiled equivalents for conforming programs.

---

## 6. User Stories

### US-01: Kernel Developer Compiling Boot Code

> As a **kernel developer**, I want to compile i386 C source with inline assembly so that I can build kernel modules without depending on GCC.

**REQ-US-01-A** *(Event-driven)*
When the user invokes `cc -m32 -std=gnu11 -c kernel.c -o kernel.o`, the compiler shall produce i386 assembly using cdecl calling conventions and pass it to the assembler.

**REQ-US-01-B** *(Ubiquitous)*
The compiler shall correctly lower inline assembly with register constraints, memory clobbers, and `asm goto` label references.

**REQ-US-01-C** *(Ubiquitous)*
The compiler shall handle kernel-style macros including `typeof`, `__builtin_offsetof`, `__builtin_constant_p`, and `__attribute__((packed))`.

### US-02: Application Developer Building Bash

> As an **application developer**, I want to compile Bash with `cc` so that I can validate the compiler against a complex, real-world C codebase.

**REQ-US-02-A** *(Ubiquitous)*
The compiler shall handle Bash's use of `__sync_*` atomics, complex struct types, function pointers, and deeply nested macros.

**REQ-US-02-B** *(Ubiquitous)*
The compiler shall produce an executable that passes Bash's own test harness.

**REQ-US-02-C** *(Ubiquitous)*
The compiler shall produce an executable that runs interactively without crashes.

### US-03: Library Developer Using C23 Features

> As a **library developer**, I want to use C23 features like `typeof`, `nullptr`, `constexpr`, `[[nodiscard]]`, and `_BitInt` so that my code is cleaner and more type-safe.

**REQ-US-03-A** *(Event-driven)*
When `-std=c23` is active, the compiler shall accept `typeof(expr)` and `typeof_unqual(expr)` in declaration specifiers.

**REQ-US-03-B** *(Event-driven)*
When `-std=c23` is active, the compiler shall accept `nullptr` as a null pointer constant of type `nullptr_t`.

**REQ-US-03-C** *(Event-driven)*
When `-std=c23` is active, the compiler shall accept `constexpr` on object declarations and enforce compile-time evaluation.

**REQ-US-03-D** *(Event-driven)*
When `-std=c23` is active, the compiler shall accept `[[nodiscard]]` and emit a warning when a nodiscard return value is discarded.

### US-04: Build System Maintainer

> As a **build system maintainer**, I want `cc` to be a drop-in replacement for GCC so that existing Makefiles work without modification.

**REQ-US-04-A** *(Ubiquitous)*
The compiler shall accept all command-line options documented in this specification with semantics compatible with GCC.

**REQ-US-04-B** *(Event-driven)*
When an unrecognized warning flag is encountered, the compiler shall emit a warning and continue.

**REQ-US-04-C** *(Ubiquitous)*
The compiler shall produce object files that link correctly with GCC-compiled object files via GNU `ld` or Substrate `ld`.

### US-05: Substrate Self-Hosting

> As a **Substrate developer**, I want `cc` to compile all Substrate userland code (libc, libsys, shell, binutils) so that the OS can be self-hosting.

**REQ-US-05-A** *(Ubiquitous)*
The compiler shall correctly compile all code in `lib/c/`, `lib/sys/`, `bin/`, and `sbin/` without regressions.

**REQ-US-05-B** *(Ubiquitous)*
The compiler shall support `-nostdlib`, `-fno-builtin`, and `-fno-pie` for kernel and bare-metal compilation.

---

## 7. Developer Stories

### DS-01: Adding a New Type

> As a **compiler developer**, I want the type system to be extensible so that adding a new type (e.g., `_Float16`) requires adding a type enum value and size/alignment entries, not restructuring the entire frontend.

**REQ-DS-01-A** *(Ubiquitous)*
The type representation shall support arbitrary pointer depth without enumerated type variants.

**REQ-DS-01-B** *(Ubiquitous)*
The type representation shall support composite types (struct/union/enum) as first-class type entries with associated metadata (tag, member list, size, alignment).

**REQ-DS-01-C** *(Ubiquitous)*
Adding a new scalar type shall require changes only in: type enum definition, size/alignment table, and codegen emission — not in the parser or semantic analyzer.

### DS-02: Adding a New GNU Builtin

> As a **compiler developer**, I want to add a new `__builtin_*` function so that headers using it compile correctly.

**REQ-DS-02-A** *(Ubiquitous)*
The builtin registry shall be a declarative table mapping name → argument count, argument types, return type, and codegen action.

**REQ-DS-02-B** *(Ubiquitous)*
Adding a builtin shall require only a table entry and an optional codegen handler, not modifications to the parser.

### DS-03: Adding a New Target ABI

> As a **compiler developer**, I want ABI logic to be isolated so that adding ARM support requires a new backend module without modifying the frontend or middle-end.

**REQ-DS-03-A** *(Ubiquitous)*
The backend shall define an ABI interface: argument classification, return classification, frame layout, and register set.

**REQ-DS-03-B** *(Ubiquitous)*
The middle-end shall query the ABI interface for type sizes, alignments, and calling convention details without hardcoding target-specific values.

### DS-04: Testing a Language Feature

> As a **compiler developer**, I want to write a test that specifies C source, expected diagnostics, and expected assembly patterns so that regressions are caught automatically.

**REQ-DS-04-A** *(Ubiquitous)*
The test framework shall support positive tests (source that compiles and runs correctly).

**REQ-DS-04-B** *(Ubiquitous)*
The test framework shall support negative tests (source that must produce specific error diagnostics).

**REQ-DS-04-C** *(Ubiquitous)*
The test framework shall support codegen tests (assembly output contains specific patterns).

**REQ-DS-04-D** *(Ubiquitous)*
The test framework shall support differential tests (output matches GCC/Clang for the same input).

### DS-05: Fuzzing the Frontend

> As a **compiler developer**, I want a fuzz harness for the parser and semantic analyzer so that crashes on malformed input are discovered early.

**REQ-DS-05-A** *(Ubiquitous)*
The compiler shall provide a fuzz harness that accepts arbitrary byte buffers as C source and exercises the full frontend pipeline (preprocess → lex → parse → sema).

**REQ-DS-05-B** *(Ubiquitous)*
The fuzz harness shall exercise both `-m32` and `-m64` targets.

**REQ-DS-05-C** *(Ubiquitous)*
The compiler shall be crash-free for all inputs discovered by fuzzing (zero ASAN/UBSAN findings).

### DS-06: Debugging a Codegen Bug

> As a **compiler developer**, I want to dump the SSA IR at each optimization pass so that I can diagnose miscompilations.

**REQ-DS-06-A** *(Event-driven)*
When `-emit-ssa` is specified, the compiler shall dump the IR after lowering to a `.ir` file.

**REQ-DS-06-B** *(Ubiquitous)*
The `ir-verifier` tool shall validate SSA form, type consistency, and use-def chains.

**REQ-DS-06-C** *(Ubiquitous)*
The `ir-diff` tool shall compare two IR dumps and highlight semantic differences.

**REQ-DS-06-D** *(Ubiquitous)*
The `ir-normalize` tool shall canonicalize temporary names for stable diff comparison.

---

## 8. Traceability Matrix

| Requirement        | User Story           | Developer Story | Tasklist Section           |
|--------------------|----------------------|-----------------|----------------------------|
| REQ-CC-010–026     | US-04, US-05         |                 | CLI / Driver               |
| REQ-CC-030–039     | US-01, US-02, US-04  |                 | Preprocessor               |
| REQ-CC-050–060     | US-03                | DS-01           | §0 (Type System Debt)      |
| REQ-CC-070–076     |                      |                 | §1.1 (Lexer)               |
| REQ-CC-080–091     | US-01, US-02, US-03  |                 | §1.2–1.5 (Parser)          |
| REQ-CC-100–110     | US-01, US-02         |                 | §1.4, 1.9 (Sema)           |
| REQ-CC-120–130     | US-01, US-02         | DS-06           | Middle-end / IR            |
| REQ-CC-140–149     | US-01, US-05         | DS-03           | Backend / Codegen          |
| REQ-CC-160–171     | US-01, US-02         | DS-02           | §5 (GNU Extensions)        |
| REQ-CC-180–182     | US-03                |                 | §2.4 (C23 Attributes)      |
| REQ-CC-190–193     | US-02                |                 | §6 (Clang Compat)          |
| REQ-CC-200–205     | US-04                |                 | Diagnostics                |
| REQ-CC-300–308     | US-04, US-05         | DS-05           | Non-functional             |

---

## 9. Acceptance Criteria

1. `cc -m32 -std=gnu11 -c test.c -o test.o` produces a valid i386 object linkable by Substrate `ld` and GNU `ld`.
2. `cc -m64 -std=c23 -c test.c -o test.o` accepts all C23 language features and produces a valid x86-64 object.
3. `cc -E` produces preprocessor output identical to GCC for a representative corpus of header-heavy inputs.
4. Bash compiles, passes its own test harness, and runs interactively without crashes.
5. All Substrate userland (`lib/c/`, `lib/sys/`, `bin/`, `sbin/`) compiles without `--bootstrap-gcc` fallback.
6. Struct/union pass-by-value and return-by-value conform to SysV ABI on both i386 and x86-64.
7. `__sync_*` and `__atomic_*` builtins produce correct atomic machine code.
8. Standard `[[attribute]]` syntax is parsed and acted upon in C23 mode.
9. Two identical compilation runs produce byte-identical assembly output.
10. The fuzz harness runs for 24 hours without discovering any crashes (zero ASAN/UBSAN findings).
11. Differential compile tests vs GCC/Clang pass for the full test corpus.
