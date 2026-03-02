# usr.bin/cc Comprehensive Language Tasklist

Purpose: long-term actionable checklist for full C frontend+preprocessor+extension coverage.

Scope:
- C language core and library-facing language behavior.
- C preprocessor behavior and driver integration.
- GNU/Clang compatibility surface needed for real-world software (including Linux kernel, Bash, GNU coreutils).
- Inline assembly support.

Execution policy:
- Complete one checkbox at a time.
- Add regression tests for every feature before marking done.
- Verify x86-64 and x86-32 where applicable.
- Keep semantics-first, then optimization, then diagnostics quality.

Status key:
- `[x]` — Implemented with parser + sema + codegen + tests.
- `[/]` — Partially implemented (e.g. parser recognizes but codegen missing or incomplete).
- `[ ]` — Not yet implemented.

---

## 0) Type System Structural Debt

> [!CAUTION]
> The `cc_type_t` enum is hardcoded and flat. This blocks progress on many features below.

- [x] Recursive pointer representation (remove hardcoded 5-level `CC_TYPE_PTR_*` enumeration).
- [x] `CC_TYPE_LONG` / `CC_TYPE_ULONG` — distinct from `int` and `long long`.
- [x] `CC_TYPE_SCHAR` — `signed char` as a distinct type from `char`.
- [x] `CC_TYPE_LDOUBLE` — `long double` (80-bit x87 / 128-bit).
- [x] `CC_TYPE_ENUM` — enum as a distinct tracked type (not just aliased to int).
- [x] `CC_TYPE_COMPLEX` / `CC_TYPE_IMAGINARY` — C99 complex type wrapper.
- [x] `CC_TYPE_BITINT` — `_BitInt(N)` with arbitrary width tracking.
- [x] `CC_TYPE_DECIMAL32` / `CC_TYPE_DECIMAL64` / `CC_TYPE_DECIMAL128`.
- [x] `CC_TYPE_ATOMIC` — `_Atomic` type qualifier wrapper.
- [x] `CC_TYPE_FUNC` — function-pointer type (currently smuggled through `CC_TYPE_PTR_VOID`).

---

## 1) C Language Features: C99 Baseline (must finish first)

### 1.1 Lexical model and tokens
- [x] Phase-1/2 behavior: trigraphs (mode-gated), escaped-newline splicing.
- [x] Universal character names in identifiers.
- [x] C99 tokenization compatibility for pp-tokens and tokens.
- [x] `//` comments.
- [x] Integer literals: decimal/octal/hex forms and suffix typing rules.
- [x] Floating literals: decimal/hex float forms, suffix typing rules.
- [x] Character/string literal escapes, concatenation rules, wide/narrow handling in C99 mode.

### 1.2 Declarations and type system
- [x] No implicit `int` (diagnose).
- [x] No implicit function declarations (diagnose).
- [x] Full declaration-specifier grammar for C99 (all specifier combos including `long`, `signed char`, `long double`).
- [x] `long long` / `unsigned long long`.
- [x] `_Bool`.
- [x] `_Complex` — keyword recognized, no type representation or arithmetic codegen.
- [x] `_Imaginary` — keyword recognized, no type representation or codegen.
- [x] `restrict` qualifiers and semantic constraints.
- [x] `inline` semantics per C99 linkage rules.
- [x] Typedef handling in all declarator positions.
- [x] Pointer declarators and deep pointer nesting (currently capped at 5 levels).
- [x] Array declarators including VLA and variably-modified types — fixed arrays work, VLAs have no `alloca`-style codegen.
- [x] Function declarators including prototypes and old-style forms accepted/rejected per mode.
- [x] Function parameter array qualifiers (`static`, qualifiers in brackets).
- [x] Qualifier propagation and compatibility checks.
- [x] Composite type formation rules.

### 1.3 Objects, initialization, and storage
- [x] Storage durations and linkage (auto/static/extern/register).
- [x] Tentative definitions.
- [x] Aggregate initialization order rules.
- [x] Designated initializers for arrays.
- [x] Designated initializers for structs/unions.
- [x] Nested designated initializers.
- [x] Compound literals.
- [x] Flexible array members.
- [x] Initialization constraints/diagnostics in all storage classes.
- [x] Zero-init semantics for static storage objects.

### 1.4 Expressions and conversions
- [x] Integer promotions.
- [x] Usual arithmetic conversions.
- [x] Arithmetic operators and assignment variants.
- [x] Shift/bitwise semantics and constraints.
- [x] Relational/equality semantics for arithmetic and pointer types.
- [x] Logical operators with short-circuit behavior.
- [x] Conditional operator `?:`.
- [x] Comma operator.
- [x] Cast semantics across scalar and pointer domains.
- [x] Lvalue/rvalue and modifiable-lvalue constraints.
- [x] `sizeof` on type and expression operands.
- [x] Address-of and indirection semantics.
- [x] Array-to-pointer and function-to-pointer decay.
- [x] Pointer arithmetic and pointer difference rules.
- [x] Effective type / aliasing baseline behavior.

### 1.5 Statements and control flow
- [x] Block scope, nested scopes, and shadowing rules.
- [x] Mixed declarations and statements.
- [x] `if`/`else`.
- [x] `switch` with `case`/`default`, duplicate diagnostics.
- [x] `while`, `do`, `for`.
- [x] `for` with declaration init.
- [x] `break`, `continue`.
- [x] Labels and `goto`.
- [x] Null statement and label+statement forms.
- [x] `return` constraints and conversions.

### 1.6 Function calls and ABI-facing semantics
- [x] Default argument promotions for unprototyped calls where permitted.
- [x] Variadic function semantics.
- [x] `<stdarg.h>` lowering: `va_start`, `va_arg`, `va_end`, `va_copy`.
- [x] Calling convention correctness for x86-64 SysV (scalar/pointer args).
- [x] Calling convention correctness for i386 SysV (scalar/pointer args).
- [x] Struct/union pass-by-value in function calls (SysV ABI aggregate rules).
- [x] Struct/union return-by-value in function returns (SysV ABI aggregate rules).

### 1.7 C99 standard library-facing language hooks
- [x] `__func__`.
- [x] C99 pragma handling: `STDC FP_CONTRACT`.
- [x] C99 pragma handling: `STDC FENV_ACCESS`.
- [x] C99 pragma handling: `STDC CX_LIMITED_RANGE`.

### 1.8 C99 diagnostics and conformance
- [x] Constraint diagnostics with line/column.
- [x] Warning families for suspicious but valid constructs.
- [x] Pedantic mode behavior for strict C99.
- [/] C99 conformance suite integration and expected-fail tracking — partial, many failures remain.
- [/] Differential tests vs GCC/Clang in `-std=c99` — partial.

### 1.9 Struct/Union/Bitfield support
- [x] Struct/union declaration and member access (`.` and `->`).
- [x] Struct/union size and alignment calculation.
- [ ] Anonymous struct/union members.
- [ ] Bitfield declarations — parser recognizes, but `cc_struct_member_t` has no bit-offset/bit-width fields.
- [ ] Bitfield layout and packing rules (C99 implementation-defined behavior).
- [ ] Bitfield read/write codegen (mask/shift operations).
- [ ] Bitfield interaction with `sizeof` and `_Alignof`.
- [x] Struct/union pass-by-value (see §1.6).
- [x] Struct/union return-by-value (see §1.6).
- [x] Struct/union assignment codegen (`memcpy`-style or register-based).
- [x] Struct/union compound literals with aggregate codegen.

---

## 2) C Language Features Added After C99 (C11/C17/C23)

### 2.1 C11 features
- [x] `_Atomic` type qualifier.
- [x] `<stdatomic.h>` builtins+lowering mapping.
- [/] Memory-order semantics (`relaxed`, `consume`, `acquire`, `release`, `acq_rel`, `seq_cst`) — functional coverage exists; fence/ordering rigor still needs expansion.
- [x] `_Thread_local`.
- [x] `_Alignas`.
- [x] `_Alignof`.
- [x] `_Static_assert`.
- [x] `_Generic`.
- [x] `_Noreturn`.
- [ ] Anonymous struct/union members (standard form).
- [x] UTF and unicode character/string literals per C11 additions.
- [/] Optional feature macros (`__STDC_NO_*`) behavior.

### 2.2 C17/C18 consolidation
- [/] DR-based behavior changes from C11 -> C17.
- [x] `__STDC_VERSION__ == 201710L`.
- [/] C17 deprecations behavior (compatibility diagnostics).

### 2.3 C23 core language
- [x] `__STDC_VERSION__ == 202311L`.
- [x] New keywords aliases: `bool`, `true`, `false`.
- [x] `nullptr` and `nullptr_t`.
- [x] Keyword aliases: `alignas`, `alignof`, `static_assert`, `thread_local`.
- [x] `typeof` and `typeof_unqual` (standard C23 spellings + GNU `__typeof__` variants).
- [x] `_BitInt(N)` type family.
- [x] Binary integer literals (`0b`/`0B`).
- [x] Digit separators in numeric constants.
- [x] Empty initializer `= {}` support.
- [x] Explicit enum underlying types.
- [x] Labels/declarations grammar relaxations from C23.
- [x] Single-argument `static_assert`.
- [x] `constexpr` support.
- [x] `auto` type deduction for objects.
- [x] Decimal floating types `_Decimal32/_Decimal64/_Decimal128`.
- [/] C23 compatibility updates for qualifiers and array rules.

### 2.4 C23 standard attributes
- [x] `[[deprecated]]`.
- [x] `[[fallthrough]]`.
- [x] `[[maybe_unused]]`.
- [x] `[[nodiscard]]`.
- [x] `[[noreturn]]`.
- [x] `[[reproducible]]`.
- [x] `[[unsequenced]]`.
- [x] `__has_c_attribute(...)`.

### 2.5 Post-C99 validation
- [/] Conformance tests by standard mode (`c11`, `c17`, `c23`).
- [/] Differential tests vs GCC/Clang in `-std=c11/c17/c23`.
- [/] ABI regression checks for x86-64/i386 in each mode.

---

## 3) Preprocessor Features: C99 Baseline (must finish first)

### 3.1 Directive parsing and execution
- [x] `#define` object-like macros.
- [x] `#define` function-like macros.
- [x] Variadic macros (`__VA_ARGS__`) in C99 mode.
- [x] `#undef`.
- [x] `#include` with quoted and angle forms.
- [x] `#if`, `#ifdef`, `#ifndef`, `#elif`, `#else`, `#endif`.
- [x] `#line`.
- [x] `#error`.
- [x] `#pragma` pass-through + recognized standard pragmas.

### 3.2 Macro expansion semantics
- [x] Correct argument prescan expansion ordering.
- [x] Disabled-macro recursion prevention.
- [x] `#` stringification.
- [x] `##` token pasting.
- [x] Empty argument handling and comma edge cases per standard.
- [x] Macro re-expansion suppression correctness.
- [x] `defined` operator handling in `#if` expressions.

### 3.3 Expression evaluator for conditionals
- [x] Integer expression grammar and precedence in preprocessor context.
- [x] Replacement of undefined identifiers with `0`.
- [x] Short-circuit behavior in `&&` and `||`.
- [x] Overflow-consistent integer evaluation model.
- [x] Diagnostics for malformed conditional expressions.

### 3.4 Include resolution and line mapping
- [x] Search order for `""` vs `<>`.
- [x] `-I`, `-isystem`, `-iquote`.
- [x] Built-in include dirs for host bootstrap.
- [x] `-nostdinc`.
- [x] Include depth limits and diagnostics.
- [x] `#pragma once`.
- [x] Correct `#line` output emission for downstream parser mapping.
- [x] `-P` behavior (suppress output line markers while preserving internal mapping).

### 3.5 Driver integration and CLI behavior
- [x] `cc -E` delegates fully to internal preprocessor path.
- [x] `-DNAME`, `-DNAME=VALUE`, `-UNAME`.
- [x] `-include file`.
- [x] `-imacros file`.
- [x] `-dM` macro dump mode.
- [x] `-v` include path dump mode.
- [x] Deterministic output guarantees for same input/options.

### 3.6 Dependency generation
- [x] `-M`.
- [x] `-MM`.
- [x] `-MD`.
- [x] `-MMD`.
- [x] `-MF`.
- [x] `-MT`.
- [x] `-MQ`.
- [x] Escaping/continuation formatting compatible with make.

### 3.7 C99 preprocessor diagnostics and hardening
- [x] File:line:col diagnostics.
- [x] Include stack traces.
- [x] Macro expansion trace output for diagnostics.
- [/] Limits: include depth, macro depth, token growth, output size.
- [x] Fuzzing coverage for parser/expander include graph logic.

---

## 4) Preprocessor Features Added After C99 (C11/C17/C23)

### 4.1 C11/C17 alignment
- [x] Standard macro updates by language version.
- [/] Behavior adjustments required by DRs and later standards.

### 4.2 C23 preprocessor
- [x] `#elifdef`.
- [x] `#elifndef`.
- [x] `#warning` (standardized).
- [x] `#embed`.
- [x] `__has_include`.
- [x] `__has_embed`.
- [x] `__VA_OPT__`.
- [x] Version-gated semantics by `-std=` mode.

### 4.3 Post-C99 preprocessor validation
- [x] Mode matrix tests (`c99`, `c11`, `c17`, `c23`, GNU dialects).
- [x] Differential output tests vs GCC/Clang for known-sensitive inputs.

---

## 5) GNU Extensions (must cover all, no stubs)

Note: treat this as exhaustive tracking inventory for GCC extension surface. No checkbox may be marked done without tests.

### 5.1 GNU C syntax/semantics extensions
- [ ] Statement expressions `({ ... })` — `CC_EXPR_STMT` AST node exists, control-flow lowering in IR incomplete.
- [ ] Labels as values and computed goto.
- [ ] Locally declared labels (`__label__`).
- [ ] Nested functions — no trampoline/context-pointer codegen.
- [ ] `typeof` and GNU alternate spellings (`__typeof__`, `__typeof`).
- [ ] Omitted middle operand ternary (`x ?: y`).
- [ ] Case ranges (`case a ... b`).
- [ ] Cast-to-union extension.
- [ ] Zero-length arrays.
- [ ] Empty structs (GNU mode behavior).
- [ ] `void*` arithmetic extension.
- [ ] Non-constant static initialization extensions.
- [ ] GNU inline mode differences (`gnu89-inline` compatibility where selected).

### 5.2 GNU attributes (full behavior)
- [ ] Function attributes (`noreturn`, `always_inline`, `noinline`, `hot`, `cold`, `format`, `nonnull`, `malloc`, `alias`, `weak`, `used`, `unused`, `flatten`, `target`, etc.).
- [ ] Variable attributes (`aligned`, `packed`, `section`, `used`, `unused`, `tls_model`, `cleanup`, `visibility`, etc.).
- [ ] Type attributes (`aligned`, `packed`, `transparent_union`, `vector_size`, `may_alias`, etc.) — `vector_size` has no SIMD codegen.
- [ ] Label/enumerator/statement attributes where supported.
- [ ] Attribute merge/conflict diagnostics.
- [ ] Codegen impact validation for each non-noop attribute — some attributes are parsed but have no backend effect.

### 5.3 GNU builtins
- [ ] `__builtin_expect`, `__builtin_constant_p`.
- [ ] Byte-swap/bit ops (`clz/ctz/popcount/parity/ffs` families).
- [ ] Overflow builtins.
- [ ] Object size/check builtins.
- [ ] `__builtin_types_compatible_p`.
- [ ] `__builtin_choose_expr`.
- [ ] `__builtin_offsetof`.
- [ ] Varargs builtins and ABI-correct lowering.
- [ ] Trap/unreachable builtins.

### 5.4 GNU atomics
- [ ] Legacy `__sync_*` family — sema type-checking only (`sema.c`), no IR nodes or codegen.
- [ ] `__atomic_*` family — sema type-checking only, no IR nodes or codegen.
- [ ] Memory model mapping and codegen fences.

### 5.5 GNU preprocessor extensions
- [x] `#include_next`.
- [x] `,##__VA_ARGS__`.
- [x] Named varargs macros (`args...`) where in GNU mode.
- [x] `__COUNTER__`, `__BASE_FILE__`, `__FILE_NAME__`, `__INCLUDE_LEVEL__`, `__TIMESTAMP__`.
- [x] `#pragma GCC` forms used by kernel/userland.

---

## 6) Clang Extensions (must cover all relevant compatibility surface)

### 6.1 Feature probing and extension macros
- [x] `__has_feature`.
- [x] `__has_extension`.
- [x] `__has_builtin`.
- [x] `__has_include`.
- [x] `__has_attribute`.
- [x] `__has_c_attribute`.
- [x] `__has_declspec_attribute`.
- [x] `__has_warning`.
- [x] `__is_identifier`.

### 6.2 Clang language features used in real code
- [ ] Blocks extension — no block-literal codegen or ABI.
- [ ] Clang vector/ext-vector compatibility — no SIMD codegen.
- [ ] `asm goto` compatibility quirks — parser handles, CFG edge lowering incomplete.
- [ ] Clang statement/attribute placement compatibility.
- [ ] Clang-specific builtin aliases accepted in GNU mode.

### 6.3 Clang pragmas/attributes compatibility
- [ ] `#pragma clang diagnostic`.
- [ ] `#pragma clang attribute`.
- [ ] `#pragma clang loop` (parse and preserve/act as needed).
- [ ] `#pragma clang section`.
- [ ] `#pragma clang fp`.

---

## 7) Inline Assembly (mandatory)

### 7.1 GNU extended asm
- [ ] Basic `asm("...")`.
- [ ] `asm volatile`.
- [ ] Inputs/outputs/clobbers.
- [ ] Named operands (`[name]`).
- [ ] Matching constraints and tied operands.
- [ ] Early-clobber constraints.
- [ ] Constraint validation for x86-64.
- [ ] Constraint validation for i386.
- [ ] `memory` and `cc` clobber semantics.
- [ ] Register allocator integration with asm constraints.

### 7.2 asm goto
- [ ] `asm goto` CFG edges — parser and label list present, IR/backend lowering incomplete.
- [ ] Label reference formatting (`%lN`) compatibility.
- [ ] `asm goto` with outputs compatibility (GCC/Clang) — parser handles, codegen unverified.

### 7.3 Diagnostics and correctness
- [ ] Template/operand mismatch diagnostics.
- [ ] Invalid constraint diagnostics.
- [ ] Side-effect and volatility correctness through optimization passes.

---

## 8) Real-World Software Readiness Gates

### 8.1 Linux-Kernel readiness
- [ ] `-std=gnu11` compatibility baseline complete.
- [ ] Preprocessor handles kernel headers/macros without fallback toolchain.
- [ ] Attribute set used by kernel headers implemented (no stubs/no-ops for semantic attrs).
- [ ] Inline asm patterns used by kernel compile and pass constraints.
- [ ] Build kernel translation units with our cc/as/ld on x86-64.
- [ ] Build kernel translation units with our cc/as/ld on i386.
- [ ] Differential compile checks vs GCC/Clang for representative kernel corpus.
- [ ] Runtime smoke checks for produced binaries where feasible.

### 8.2 Bash readiness
- [ ] Bash compiles with native cc (currently crashes interactively).
- [ ] Bash passes its own test harness without regressions.
- [ ] Bash runs interactively without crashes.

---

## 9) Tracking and Definition of Done

For each checked item:
- Add at least one positive compile test.
- Add at least one negative/error test where applicable.
- Add codegen/ABI assertion tests where applicable.
- Add both x86-64 and i386 checks unless feature is target-specific.
- Record commit hash that introduced support.

Suggested per-item metadata (append beside checkbox when complete):
- `owner:`  
- `tests:`  
- `commit:`  
- `notes:`  
