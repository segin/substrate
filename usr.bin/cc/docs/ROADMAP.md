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
- Current limitation:
  - i386 floating-point codegen is intentionally rejected with diagnostics (not yet lowered to x87/SSE ABI rules).
