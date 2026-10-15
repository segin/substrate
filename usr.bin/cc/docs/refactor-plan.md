# cc: refactoring the compiler as a whole

Status: plan.  The back half (IR, ABI, legalization, emitters) is
`neutral-ir-plan.md`; this document covers the whole compiler and
says where that plan fits.

The compiler works -- it matches gcc on every program in
`tests/usr.bin/cc/codegen`, on 40 random arithmetic seeds and on the
mixed-compiler ABI test, in -m64 and -m32 -- but it was grown feature
by feature, and its structure shows it.  The measurements below are
from the tree at the time of writing (52k lines in `frontend/`,
`middle/`, `backend/`, `cmd/`).

## 1. What is wrong, measured

**R1. C types are an enum.**  `cc_type_t` has 86 members, 60 of them
`CC_TYPE_PTR_*`: a pointer's depth is part of the enumerator.  A
struct's identity rides alongside in a separate `struct_id`, array
dimensions in a fixed `long array_dims[4]`, and there are no function
types at all.  Every consumer re-derives what a type is from the
three.  Bugs that follow: CC-34 (a function returning a pointer is
indistinguishable from a pointer to a function pointer), CC-50
(`sizeof(int(void))` cannot be parsed), a five-dimensional array is
impossible.

**R2. The same helpers, written again in each file.**  46 functions
are defined in two or more files: `set_diag` in 10, `xstrdup` in 5,
`is_pointer_type` in 4, `is_integral_type` and `ptr_base_type` in 3;
`type_size_bytes`, `integral_rank`, `integral_promo_type`,
`array_type_size_bytes`, `find_struct_member_recursive`, `builtin_kind`
and the inline-asm constraint predicates in both `sema.c` and
`ast2ir.c` (and some in `emit_s.c`).  They drift: the usual arithmetic
conversions had to be fixed in sema *and* in lowering (CC-1).

**R3. Five constant-expression evaluators.**  `parser.c`
(`eval_const_expr_value`, `eval_const_array_bound_expr`), `sema.c`
(`eval_const_int_expr`), `ast2ir.c` (`eval_const_integral_expr_value`,
`eval_const_int_expr_simple`) and the static-initializer evaluator in
`ast2ir.c`.  Making `sizeof(void)` 1 meant changing four of them.

**R4. Sema does not annotate; lowering re-decides.**  Sema checks the
program and leaves the AST as it was.  `ast2ir.c` then recomputes
every expression's type, its promotions and its conversions, with its
own copies of the rules (R2).  The two must agree and are separately
maintained.

**R5. Very large functions.**  `lower_expr` is 4,086 lines,
`lower_stmt` 2,233, `check_expr` 1,892, `parse_declspec` 1,678,
`cc_ast_to_ssa` 1,063.  14 functions exceed 500 lines, 47 exceed 200.

**R6. Global state and setter plumbing.**  29 file-scope mutable
`g_*` variables (13 in `sema.c`, 9 in `parser.c`), set through
`cc_frontend_set_*` / `cc_backend_set_i386_*` calls, and a pipeline
entry point with 20 parameters.  The pointer size is held separately
by the lexer, parser, sema, SSA and emitter, each set by the one
before it.  Those setters are reached through hand-written
declarations in the calling file (`parser.c` declares the lexer's
API; `sema.c` the parser's) -- seven of them, against the tree's rule
of including the header.

**R7. The parser does semantic work.**  `parser.c` (12k lines) keeps
its own variable table, evaluates `typeof`, folds constants (R3),
applies implicit `int`, and handles trigraphs -- which
`preproc.c` also handles (`trigraph_replacement` is in both).

**R8. Diagnostics have no owner.**  Three hand-written print paths
(sema, parser, driver), each formatting `file:line:col` itself; they
disagreed until CC-48.

**R9. Dead and stray code.**  The MIR (`backend/select.c`) is built,
validated and freed; nothing reads it.  The top-level `ir.c`,
`parser.c`, `verifier.c`, `serialize.c` and the `ir-verifier`,
`ir-normalize`, `ir-diff` tools are not dead but disconnected: they
were the first thing written here (Feb 2026), a typed textual SSA
format with a verifier for the CFG and SSA invariants (terminators,
phi arity, unique definitions, dominance, reachability), a normalizer
and a differ, tested in the suite on six fixtures -- but the compiler
was built on a different in-memory SSA and never writes or reads the
format.  The verifier checks no types or opcodes (it accepts `add` on
an `i1`, a `ret i64` from an `i32` function, and an opcode named
`frobnicate`), and its instructions are kept as strings.  They become
the neutral IR's text form (`neutral-ir-plan.md` step 0), not
deleted.  `README.md` describes
"Phase-9" and a system `cpp` (CC-33); `TASKLIST_*.md` and `SPEC.md`
have drifted.

**R10. A 2,500-line driver.**  `cmd/cc.c`'s `parse_args` is 587 lines
of `strcmp` chains, as `ld`'s was before its option table.

**R11. cc cannot build itself.**  `cc` compiles all of its own sources
without an error, but the result does not work: a `cc` with
`parser.c`, `sema.c`, `ast2ir.c` or `emit_s.c` compiled by `cc` (the
rest by gcc) crashes or miscompiles on its first program.  This is
CC-51.  It is a correctness bug, not structure -- but it is listed
here because a self-built `cc` is the gate the refactor needs (§3).

## 2. The shape we want

```
frontend/  lexer, preproc, parser (syntax only), sema -> typed AST
           types.c     one type representation, sizes from the target
           consteval.c one constant evaluator
middle/    ast2ir (typed AST -> neutral IR), passes, ABI, legalize
backend/   per-target emitters (neutral-ir-plan.md)
common/    diag.c (one diagnostic printer), util.c (strings, vectors)
cmd/       driver with an option table; one cc_context_t
```

- **Types (R1).**  Interned type objects: `void`, `_Bool`, the integer
  and floating kinds, `ptr(T)`, `array(T, n | vla)`, `func(ret,
  params, variadic, prototyped)`, `struct`/`union(record)`, `enum`,
  with qualifiers.  Equal types are the same pointer.  Size and
  alignment come from the target descriptor.  `cc_type_t`,
  `struct_id`, `array_dims` and the pointer-depth arithmetic go.
- **Typed AST (R4).**  Sema stores each expression's type and makes
  every implicit conversion an explicit node (integer promotion,
  usual arithmetic conversion, array and function decay, lvalue
  conversion, conversion to the assignment's or parameter's type).
  Lowering reads types and conversions; it does not compute them, and
  the duplicated rules in `ast2ir.c` (R2) are deleted.
- **One constant evaluator (R3)** over the typed AST, used for array
  bounds, case labels, enumerators, static initializers, `_Static_assert`
  and `#if`-independent folding.  The preprocessor's `#if` evaluator
  stays separate: it has its own rules (no types, `defined`).
- **A context (R6)**, `cc_context_t`: options, target descriptor,
  diagnostics, arenas.  Passed explicitly; no `g_*` state.
- **Parser is syntax (R7)**, plus the one thing C forces on it: knowing
  which identifiers are typedef names.  Trigraphs only in the
  preprocessor.
- **One diagnostics module (R8)**: errors, warnings, notes, `-Werror`,
  `-pedantic-errors`, the source-line caret; every phase reports
  through it.

## 3. The gate

For every step, in addition to `neutral-ir-plan.md` §4 (the suite,
`run_codegen_diff.sh`, `run_codegen_random.sh`, `run_codegen_abi.sh`,
the on-target boots):

- **Stage 2**: `cc` built by `cc` passes the same codegen checks, and
  **stage 3** (`cc` built by the stage-2 `cc`) writes byte-identical
  assembly to stage 2 for every file of `cc` itself.  A refactor that
  changes behaviour shows up here before anywhere else.
- **No new warnings** with host gcc `-std=gnu17 -Wall -Wextra`, and a
  `gcc -m32` build of `cc` (CC-20).
- **Compile time** does not regress: `CC_TIMINGS=1` on `ast2ir.c`,
  recorded per step.

## 4. Order of work

Each step is a series of commits, each one behaviour-preserving unless
it says otherwise, and each leaving the gate green.  Sizes are
relative.

| Step | Work | Size |
|---|---|---|
| 0 | **Self-hosting (R11, CC-51).**  Find and fix what cc gets wrong compiling `parser.c`, `sema.c`, `ast2ir.c`, `emit_s.c`; each bug gets a reduced test in `codegen/`.  Add the stage-2 / stage-3 check to the suite and CI. | medium, unknown |
| 1 | **Housekeeping (R9).**  Delete the MIR.  Move the textual-IR tools out of the top level into `ir/`, kept: step 7 adopts them.  Rewrite README/ARCHITECTURE/SPEC to describe what exists (CC-33). | small |
| 2 | **Common code (R2, R8).**  `common/util.c` and `common/diag.c`; delete the copies of `set_diag`, `xstrdup`, `strvec_*`.  The type predicates move to `frontend/types.c` *unchanged* (still the enum) so that step 5 has one place to change. | small |
| 3 | **Context and driver (R6, R10).**  `cc_context_t` replaces the `g_*` variables and setters; `cc_compile_c_to_s` takes the context.  Option table for the driver, as `ld` got.  *Done so far:* the pointer size comes from the target (`cc_target_pointer_size`); `-std=` is decoded once (`common/lang.c`); the settings are one `cc_options_t` (`common/options.c`) that `cc_compile_c_to_s` takes and every phase reads, and the `cc_frontend_set_*` / `cc_parser_set_*` / `cc_backend_set_i386_*` setters are gone.  Open: the driver's option table. | medium |
| 4 | **One constant evaluator (R3)**, on today's AST, replacing the five.  Its tests: every constant-expression context, both targets. | medium |
| 5 | **The type representation (R1).**  Introduce the interned types next to the enum; derive them from it first; move consumers file by file; delete the enum last.  Fixes CC-34 and CC-50 on the way; lifts the four-dimension and pointer-depth limits. | large |
| 6 | **Typed AST (R4).**  Sema inserts conversion nodes and records types; lowering stops recomputing them; the copies in `ast2ir.c` go. | large |
| 7 | **Neutral IR**, `neutral-ir-plan.md` steps 0-5.  Its step 1 (typed IR values) falls out of step 6 here. | large |
| 8 | **Split the large functions (R5).**  After 5-7 they are smaller; `lower_expr` and `check_expr` split by expression kind into their own files, `parse_declspec` into a specifier accumulator plus a resolver.  Code moves verbatim first (git-cp trick), then changes. | medium |
| 9 | **Parser to syntax (R7).**  Its variable table, `typeof` evaluation and constant folding go to sema; trigraphs only in the preprocessor. | medium |

Step 0 comes first because every later step is checked by it.  Steps
1-4 are cheap and make 5-6 smaller.  Step 5 is the one the most bugs
trace back to.

## 5. Not in this plan

- New language features (VLA `sizeof` CC-19, nested functions CC-49,
  `__int128` CC-22), except where a step fixes one on the way.
- Optimization beyond what `opt.c` does today.
- The preprocessor's internals (6.9k lines).  It has its own problems
  but a clean boundary; it gets a later audit of its own.
