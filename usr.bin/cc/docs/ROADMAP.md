# usr.bin/cc Roadmap

## Phase 0 (current)
- Driver skeleton (`cmd/cc.c`) with option parsing and stage orchestration.
- Preprocess-only (`-E`) implemented using system `cpp`.
- Assembly/object path implemented using system `as`/`ld`.
- Native subset C pipeline implemented for:
  - `int` function definitions
  - single `return` expression bodies
  - integer literals/parameters and `+ - * /` expressions
- Existing SSA utilities retained (`ir-verifier`, `ir-normalize`, `ir-diff`).

## Phase 1 (in progress)
- C99 lexer/parser/sema frontend:
  - initial subset implemented (functions, declarations, assignments, calls, returns).
- AST -> SSA lowering:
  - initial lowering implemented for expression/statement subset.
- Minimal integer backend:
  - arithmetic/return/call emission implemented for SysV AMD64 integer calling convention.
- Remaining for full phase completion:
  - richer statements (`if`/loops), comparisons/branches, pointer-aware typing, and broader expression grammar.

## Phase 2
- ABI-complete lowering for scalar args/params (register + stack overflow paths) is implemented.
- Variadic declarations/calls are implemented for current subset, including `%al` XMM-count call convention.
- Floating point (`double`) lowering and SSE2 emission are implemented.
- Debug assembly directives for source/CFI (`-g`) are implemented.
- Remaining:
  - full C99 frontend coverage (control flow, pointers/aggregates)
  - richer DWARF variable/location emission beyond assembly-level directives

## Phase 3
- Driver now supports explicit target ABI selection with `-m32` and `-m64`.
- Backend now emits:
  - x86_64 SysV path (existing int/double subset)
  - i386 cdecl path for integer subset (stack args/params, `eax` returns)
- Middle-end pass pipeline is active at `-O1+`:
  - SSA constant folding
  - dead temporary elimination for pure SSA instructions

## Phase 4
- Backend quality tuning:
  - stack-slot reuse allocator added in emitter (reuses dead SSA value slots, reducing frame size and memory traffic).
- x86 parity improvement:
  - i386 backend now supports `double` constants/arithmetic/conversions/calls/returns for current subset.
  - i386/x86_64 share the same typed SSA lowering path; backend selects ABI-specific emission.
- Regression coverage expanded:
  - `-m32` `double` assembly/object generation checks.
  - slot-pressure frame-size regression check for stack-slot reuse.

## Phase 5
- Control-flow subset enabled:
  - frontend/parser/sema support `if/else` statements and nested block statements.
  - expression grammar includes integer comparisons (`== != < <= > >=`).
  - SSA/backend support labels and conditional/unconditional branches.
- Cross-target support:
  - branch-capable emission works on both x86_64 and i386 paths.
- Current limitation:
  - assignments inside conditional blocks are rejected to avoid incorrect merge semantics before phi/memory-SSA support lands.

## Phase 6
- Loop control-flow subset enabled:
  - frontend/parser/sema support `while`, `for`, `break`, and `continue`.
  - `break`/`continue` outside loops now produce semantic errors.
- Assignment merge safety improvement:
  - variable assignments now lower to explicit `mov` updates on dedicated variable value slots, allowing assignments inside conditional/loop bodies without rejecting valid C.
- Backend parity:
  - x86_64 and i386 emitters both support `mov` SSA updates for integer and double values.
- Optimizer safety:
  - constant-propagation state is cleared at label boundaries to avoid incorrect cross-branch folding in linearized CFG streams.

## Phase 7
- C90 control-flow coverage expansion for current scalar subset:
  - frontend/parser/sema now support `do ... while (...)`.
  - frontend/parser/sema now support `switch (...) { case ...: ... default: ... }`.
  - `break` is now valid in both loops and switches.
  - `case`/`default` labels outside switch now produce semantic errors.
- Middle-end lowering expansion:
  - `do-while` lowering emits body-first loop shape with condition backedge.
  - `switch` lowering emits compare/branch dispatch chains to case/default labels.
- Regression coverage expanded:
  - positive tests for do-while and switch/fallthrough/default behavior.
  - negative tests for invalid case/default placement.

## Phase 8
- C95 lexical compatibility slice:
  - digraph braces `<%` and `%>` are accepted as `{` and `}` in native lexer.
  - trigraph sequences are normalized during source load (`??<`, `??>`, and related mappings).
- Regression coverage expanded:
  - positive tests compiling/running digraph and trigraph source forms.
  - i386 emission checks for C95 lexical forms.

## Phase 9
- Early C99 declaration/specifier coverage for the current scalar subset:
  - declaration-specifier parser accepts common C99 specifier/qualifier/storage-class tokens and normalizes to supported scalar IR types.
  - `_Bool`, `char`, `long long`, and `float` map through sema/lowering/backend paths.
  - `for (type name = ...; ...)` declaration init clauses now have loop-local scope.
- Expression grammar expansion:
  - added `%`, unary `+`/`!`, compound assignments (`+= -= *= /= %=`), and prefix/postfix `++/--`.
  - added logical `&&`/`||` with short-circuit lowering in AST->SSA via branch+label control flow and mutable SSA value slots.
- Additional C99 control/expression expansion:
  - added `goto` and labeled statements with function-scope label validation.
  - added bitwise/shift/comma operators and compound assignments (`&= |= ^= <<= >>=`) across parser/sema/SSA/backend.
  - added ternary conditional (`?:`), scalar cast expressions, pointer/integer casts, and `sizeof` for supported scalar types.
  - comparison operators now accept floating operands; lowering/backend preserve C truthiness for floating conditions (`!= 0.0`, including `-0.0`) and ordered floating comparisons.
  - function declarations/prototypes are accepted alongside definitions; sema enforces signature compatibility and rejects duplicate/conflicting definitions.
  - lexer numeric literal coverage now includes octal/hex integer forms and common integer suffix spellings; character literals with common escape sequences are accepted.
  - `++/--` lowering now preserves C expression semantics for prefix vs postfix updates (postfix returns the prior value).
  - `switch` case labels now accept integer constant expressions (not just raw literals), with semantic duplicate detection for `case` values and `default` labels.
  - unsigned scalar declaration-specifiers now map to explicit unsigned types (`unsigned char/int/long long`) and lowering/codegen preserve unsigned compare/div/mod/right-shift semantics.
  - integer literal suffixes now carry unsigned/long-long typing (`u`, `ul`, `ull` families) into sema/lowering so unsigned behavior applies to literal-only expressions.
  - `short` / `unsigned short` declaration-specifiers now map to explicit 16-bit-sized scalar types (`sizeof == 2`) in sema/lowering type metadata.
  - one-level typed pointer declarations/params/returns (`T*`) plus unary address/dereference expressions now lower through explicit SSA addr/load operations; sema supports pointer/null equality comparisons.
  - parser now accepts `void*` declarators for locals/params while preserving `void`-only empty parameter list semantics.
  - indirect pointer stores (`*p = expr`) now lower through explicit SSA store operations for supported scalar pointee types.
  - dereference compound assignments (`*p op= rhs`) are accepted for dereference lvalues in the current subset.
  - prefix/postfix updates on dereference lvalues (`++*p`, `--*p`, `(*p)++`, `(*p)--`) are accepted and lower through direct load/compute/store update nodes.
  - pointer arithmetic lowering now supports scaled `ptr +/- int`, `int + ptr`, and compatible `ptr - ptr` difference expressions for non-`void*` pointers in the current subset.
  - explicit casts now accept pointer<->pointer and pointer<->integer conversions (while still rejecting pointer<->floating casts).
  - prefix/postfix `++/--` now support identifier pointer lvalues with element-size stepping semantics.
  - ordered pointer comparisons (`< <= > >=`) are accepted for compatible pointer types and lower as unsigned address compares.
- Regression coverage expanded:
  - positive compile/run tests for logical short-circuit semantics and update/compound operators.
  - negative parser test for invalid `++/--` lvalues.
  - i386 assembly checks for the new expression forms.
  - positive compile/run tests for `goto` flow, comma operator sequencing, and bitwise/shift codegen.
  - negative tests for unknown goto target, duplicate labels, and bitwise-on-float type errors.
  - positive compile/run tests for ternary/cast/sizeof behavior and i386 assembly checks for cast lowering.
  - positive compile/run test for pointer/integer cast round-trips plus i386 cast emission sanity check.
  - positive compile/run tests for floating comparison and floating-condition truthiness, plus i386 assembly checks for SSE compare lowering.
  - positive tests for declaration-before-definition and extern-declared call emission; negative test for conflicting declarations.
  - positive compile/run tests for character literals and octal/hex integer literals.
  - positive compile/run tests for prefix/postfix increment expression values.
  - positive compile/run tests for switch case constant-expression labels.
  - negative tests for duplicate `switch` case values and duplicate `default` labels.
  - positive compile/run test for unsigned integer semantics and i386 emission checks for `divl`/`shrl`/unsigned `setcc`.
  - positive compile/run test for unsigned literal-suffix semantics and i386 emission checks for unsigned compare codegen from literal expressions.
  - negative parser test for conflicting `signed`+`unsigned` declaration specifiers.
  - positive compile/run test for `short`/`unsigned short` scalar behavior and `sizeof`; negative parser test for invalid `short long` specifier combinations.
  - positive compile/run tests for local pointer dereference and pointer-parameter calls; i386 emission checks for addr/load codegen.
  - positive compile/run tests for pointer indirect stores (local + parameter path); i386 emission check for indirect store codegen.
  - positive compile/run test for pointer arithmetic semantics with heap-backed pointer indexing plus x86_64/i386 emission checks for scaled index arithmetic.
  - positive compile/run test for compatible pointer subtraction semantics with i386 emission check for element-size division.
  - positive compile/run test for pointer prefix/postfix `++/--` semantics over pointer lvalues.
  - positive compile/run test for `void*` declaration/assignment/conversion flow in the current subset.
  - positive compile/run test for ordered pointer comparisons over compatible pointers.
  - positive compile/run test for dereference compound assignment operations including pointer-expression dereference lvalues.
  - positive compile/run test for prefix update over dereference lvalues (including pointer-expression dereference forms).
  - negative tests for dereferencing non-pointer expressions and invalid address-of non-lvalue expressions.
  - negative tests for unsupported pointer-plus-pointer arithmetic and pointer-plus-float arithmetic.
  - negative test for incompatible pointer subtraction across mismatched pointer base types.
  - negative test for unsupported pointer<->floating cast conversion.
  - negative test for incompatible ordered pointer comparison across mismatched pointer base types.
  - negative test for invalid compound-assignment lvalue forms (non-assignable expressions).
  - negative test for unsupported `void*` arithmetic.
  - negative test for invalid postfix `++/--` lvalue forms (non-assignable expressions).
  - negative test for unsupported `sizeof(void)` in current subset.
