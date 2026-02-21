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
