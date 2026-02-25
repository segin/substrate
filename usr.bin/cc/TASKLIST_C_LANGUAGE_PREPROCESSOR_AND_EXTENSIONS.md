# usr.bin/cc Comprehensive Language Tasklist

Purpose: long-term actionable checklist for full C frontend+preprocessor+extension coverage.

Scope:
- C language core and library-facing language behavior.
- C preprocessor behavior and driver integration.
- GNU/Clang compatibility surface needed for real-world software (including Linux kernel).
- Inline assembly support.

Execution policy:
- Complete one checkbox at a time.
- Add regression tests for every feature before marking done.
- Verify x86-64 and x86-32 where applicable.
- Keep semantics-first, then optimization, then diagnostics quality.

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
- [ ] Full declaration-specifier grammar for C99.
- [x] `long long` / `unsigned long long`.
- [x] `_Bool`.
- [x] `_Complex`.
- [x] `_Imaginary`.
- [ ] `restrict` qualifiers and semantic constraints.
- [ ] `inline` semantics per C99 linkage rules.
- [ ] Typedef handling in all declarator positions.
- [ ] Pointer declarators and deep pointer nesting.
- [x] Array declarators including VLA and variably-modified types.
- [x] Function declarators including prototypes and old-style forms accepted/rejected per mode.
- [x] Function parameter array qualifiers (`static`, qualifiers in brackets).
- [ ] Qualifier propagation and compatibility checks.
- [ ] Composite type formation rules.

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
- [x] Calling convention correctness for x86-64 SysV.
- [x] Calling convention correctness for i386 SysV.

### 1.7 C99 standard library-facing language hooks
- [x] `__func__`.
- [x] C99 pragma handling: `STDC FP_CONTRACT`.
- [x] C99 pragma handling: `STDC FENV_ACCESS`.
- [x] C99 pragma handling: `STDC CX_LIMITED_RANGE`.

### 1.8 C99 diagnostics and conformance
- [x] Constraint diagnostics with line/column.
- [x] Warning families for suspicious but valid constructs.
- [x] Pedantic mode behavior for strict C99.
- [x] C99 conformance suite integration and expected-fail tracking.
- [x] Differential tests vs GCC/Clang in `-std=c99`.

---

## 2) C Language Features Added After C99 (C11/C17/C23)

### 2.1 C11 features
- [x] `_Atomic` and atomics type qualifier model.
- [x] `<stdatomic.h>` builtins+lowering mapping.
- [x] Memory-order semantics (`relaxed`, `consume`, `acquire`, `release`, `acq_rel`, `seq_cst`).
- [x] `_Thread_local`.
- [x] `_Alignas`.
- [x] `_Alignof`.
- [x] `_Static_assert`.
- [x] `_Generic`.
- [x] `_Noreturn`.
- [x] Anonymous struct/union members (standard form).
- [x] UTF and unicode character/string literals per C11 additions.
- [x] Optional feature macros (`__STDC_NO_*`) behavior.

### 2.2 C17/C18 consolidation
- [x] DR-based behavior changes from C11 -> C17.
- [x] `__STDC_VERSION__ == 201710L`.
- [x] C17 deprecations behavior (compatibility diagnostics).

### 2.3 C23 core language
- [x] `__STDC_VERSION__ == 202311L`.
- [x] New keywords aliases: `bool`, `true`, `false`.
- [x] `nullptr` and `nullptr_t`.
- [x] Keyword aliases: `alignas`, `alignof`, `static_assert`, `thread_local`.
- [x] `typeof` and `typeof_unqual` (standard C23 spellings).
- [x] `_BitInt(N)` type family and conversions.
- [x] Binary integer literals (`0b`/`0B`).
- [x] Digit separators in numeric constants.
- [x] Empty initializer `= {}` support.
- [x] Explicit enum underlying types.
- [x] Labels/declarations grammar relaxations from C23.
- [x] Single-argument `static_assert`.
- [x] `constexpr` support (C23 object semantics).
- [x] `auto` type deduction for objects (C23 semantics).
- [x] Decimal floating types `_Decimal32/_Decimal64/_Decimal128`.
- [x] C23 compatibility updates for qualifiers and array rules.

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
- [x] Conformance tests by standard mode (`c11`, `c17`, `c23`).
- [x] Differential tests vs GCC/Clang in `-std=c11/c17/c23`.
- [x] ABI regression checks for x86-64/i386 in each mode.

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
- [x] Limits: include depth, macro depth, token growth, output size.
- [x] Fuzzing coverage for parser/expander include graph logic.

---

## 4) Preprocessor Features Added After C99 (C11/C17/C23)

### 4.1 C11/C17 alignment
- [x] Standard macro updates by language version.
- [x] Behavior adjustments required by DRs and later standards.

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
- [ ] Statement expressions `({ ... })`.
- [ ] Labels as values and computed goto.
- [ ] Locally declared labels (`__label__`).
- [ ] Nested functions.
- [ ] `typeof` and GNU alternate spellings.
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
- [ ] Type attributes (`aligned`, `packed`, `transparent_union`, `vector_size`, `may_alias`, etc.).
- [ ] Label/enumerator/statement attributes where supported.
- [ ] Attribute merge/conflict diagnostics.
- [ ] Codegen impact validation for each non-noop attribute.

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
- [ ] Legacy `__sync_*` family.
- [ ] `__atomic_*` family.
- [ ] Memory model mapping and codegen fences.

### 5.5 GNU preprocessor extensions
- [ ] `#include_next`.
- [ ] `,##__VA_ARGS__`.
- [ ] Named varargs macros (`args...`) where in GNU mode.
- [ ] `__COUNTER__`, `__BASE_FILE__`, `__FILE_NAME__`, `__INCLUDE_LEVEL__`, `__TIMESTAMP__`.
- [ ] `#pragma GCC` forms used by kernel/userland.

---

## 6) Clang Extensions (must cover all relevant compatibility surface)

### 6.1 Feature probing and extension macros
- [ ] `__has_feature`.
- [ ] `__has_extension`.
- [ ] `__has_builtin`.
- [ ] `__has_include`.
- [ ] `__has_attribute`.
- [ ] `__has_c_attribute`.
- [ ] `__has_declspec_attribute`.
- [ ] `__has_warning`.
- [ ] `__is_identifier`.

### 6.2 Clang language features used in real code
- [ ] Blocks extension.
- [ ] Clang vector/ext-vector compatibility.
- [ ] `asm goto` compatibility quirks.
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
- [ ] `asm goto` CFG edges.
- [ ] Label reference formatting (`%lN`) compatibility.
- [ ] `asm goto` with outputs compatibility (GCC/Clang).

### 7.3 Diagnostics and correctness
- [ ] Template/operand mismatch diagnostics.
- [ ] Invalid constraint diagnostics.
- [ ] Side-effect and volatility correctness through optimization passes.

---

## 8) Linux-Kernel Readiness Gates

- [ ] `-std=gnu11` compatibility baseline complete.
- [ ] Preprocessor handles kernel headers/macros without fallback toolchain.
- [ ] Attribute set used by kernel headers implemented (no stubs/no-ops for semantic attrs).
- [ ] Inline asm patterns used by kernel compile and pass constraints.
- [ ] Build kernel translation units with our cc/as/ld on x86-64.
- [ ] Build kernel translation units with our cc/as/ld on i386.
- [ ] Differential compile checks vs GCC/Clang for representative kernel corpus.
- [ ] Runtime smoke checks for produced binaries where feasible.

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
