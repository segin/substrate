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
  - added ternary conditional (`?:`), scalar cast expressions, and `sizeof` for supported scalar types.
  - comparison operators now accept floating operands; lowering/backend preserve C truthiness for floating conditions (`!= 0.0`, including `-0.0`) and ordered floating comparisons.
  - function declarations/prototypes are accepted alongside definitions; sema enforces signature compatibility and rejects duplicate/conflicting definitions.
- Regression coverage expanded:
  - positive compile/run tests for logical short-circuit semantics and update/compound operators.
  - negative parser test for invalid `++/--` lvalues.
  - i386 assembly checks for the new expression forms.
  - positive compile/run tests for `goto` flow, comma operator sequencing, and bitwise/shift codegen.
  - negative tests for unknown goto target, duplicate labels, and bitwise-on-float type errors.
  - positive compile/run tests for ternary/cast/sizeof behavior and i386 assembly checks for cast lowering.
  - positive compile/run tests for floating comparison and floating-condition truthiness, plus i386 assembly checks for SSE compare lowering.
  - positive tests for declaration-before-definition and extern-declared call emission; negative test for conflicting declarations.
  - negative test for unsupported `sizeof(void)` in current subset.
