# Audit of the in-tree compiler (`usr.bin/cc`) — requirements checklist

Audit of the hand-written C compiler restored in `3169d142b`, begun
2026-10-14 at `cd7c7d4b5`.  The compiler is 40 C files, 55,155 lines:
`cmd/cc.c` (the driver), `frontend/` (preprocessor, lexer, parser,
sema), `middle/ast2ir.c` (14,705 lines: lowering to SSA),
`middle/passes/opt.c` (constant folding and dead-code removal) and
`backend/emit_s.c` (6,346 lines: x86-64 and i386 assembly).  It writes
assembly for the in-tree `as` and links with the in-tree `ld`.

Every finding is written as the requirement the compiler does not meet,
in EARS form, as in [as-audit.md](as-audit.md).  A box is ticked when
the requirement is met and a test shows it; the commit that met it is
named.

- **Basis**: *reproduced* — a program run, cc's build of it against the
  host gcc's; *traced* — the code path followed in full; *suite* — a
  command of the restored `tests/usr.bin/cc` suite fails.

How it was done:

1. A feature matrix of 41 small programs, each printing what it
   computes, built by cc and by gcc for -m64 and -m32 at -O0 and -O2.
2. Random arithmetic programs (`tests/usr.bin/cc/gen_arith.py`): 1,600
   builds at the start, of which 406 printed the wrong checksum (every
   -m32 seed and three at -m64).  Each failure was traced to its first
   wrong statement and reduced by hand.
3. The compiler's own suite, `tests/usr.bin/cc`, removed with the
   toolchain in `f2e3fe0d4` and restored for the audit.
4. The image's cc run on substrate, building and running four of the
   codegen programs at -O0 and -O2.

All arithmetic findings were *reproduced*; the failures below were
silent: the program compiled, ran and printed a wrong answer.

## 0. Summary

| Area | Requirements | Met |
|---|---|---|
| 1. Integer arithmetic and conversions | 8 | 8 |
| 2. Programs on substrate | 7 | 7 |
| 3. Tests | 2 | 1 |
| 4. ABI and other code generation | 13 | 6 |
| 5. Driver and predefined macros | 3 | 1 |
| 6. The restored suite's failing commands | 10 | 0 |
| 7. The restored suite itself | 12 | 5 |
| 8. Documentation | 1 | 1 |

The regression tests are `tests/usr.bin/cc/codegen/*.c`, run by
`run_codegen_diff.sh`, and `run_codegen_random.sh`; CI's
`substrate-toolchain` workflow runs both (job `cc-codegen`).

## 1. Integer arithmetic and conversions

- [x] **CC-1** When an integer operation mixes signedness, the compiler
  shall apply C11 6.3.1.8: a signed type of higher rank wins when it can
  hold every value of the unsigned one.  Both copies of the rule made
  `long long` with any unsigned type `unsigned long long`, so
  `(unsigned)3 >= -5LL` was false.  The cause of every -m64 random
  failure.  *Reproduced.*  `91fdc67d5`; `codegen/arith_conv_mixed_sign.c`.
- [x] **CC-2** On i386 the compiler shall compute 64-bit `+ - * / % <<
  >>` and the bitwise operators on both words.  Lowering never recorded
  size 8 for an arithmetic result, so each was computed in 32 bits and
  sign-extended; MUL and DIV had no 64-bit form.  *Reproduced.*
  `ad09b201a`; `codegen/int64_ops.c`.
- [x] **CC-3** When a 64-bit value is converted to a 32-bit type on
  i386, the compiler shall leave the value's own computation 64-bit.
  The conversion relabelled the value, making `(int)(x << 48)` a 32-bit
  shift by 16.  It also broke signed bit-field reads.  *Reproduced.*
  `f356ca7c4`; `codegen/narrow_cast.c`.
- [x] **CC-4** On i386, 64-bit constants made by lowering shall keep
  their high word.  `hi << 32 | lo` shifted by 0 in 32 bits; bit-field
  stores then cleared nothing.  *Reproduced.*  `e8d8ee559`;
  `codegen/bitfield_store.c`.
- [x] **CC-5** On i386 the compiler shall compare 64-bit integers on
  both words, in compares and in conditions.  One `cmpl` compared the
  low words.  *Reproduced.*  `690787896`; `codegen/int64_compare.c`.
- [x] **CC-6** The optimizer shall fold an integer operation at the
  width the target computes it.  Every fold was 64-bit, so on i386
  `(signed char)254` folded to 254; folding also had host undefined
  behaviour, and `LLONG_MIN / -1` crashed the compiler.  *Reproduced.*
  `d28ff5cfa`; `codegen/fold_width.c`.
- [x] **CC-7** When a value is converted to `_Bool`, the result shall be
  1 for every non-zero value.  A floating value was truncated first
  (`_Bool b = 0.5` gave 0), in assignments, declarations and constant
  initializers; a 64-bit value was tested on its low word on i386.
  *Reproduced.*  `d09a6bb97`; `codegen/bool_convert.c`.
- [x] **CC-8** Where the compiler itself runs on a 32-bit host, it shall
  write 64-bit initializers whole.  `emit_integer_data` took a `long`;
  on substrate `long long a = 0x123456789` was written as `0x23456789`.
  *Reproduced* (on substrate and with a `gcc -m32` build).  `f02061db3`.

## 2. Programs on substrate

- [x] **CC-9** A program the compiler builds on i386 substrate shall
  run.  Every one, hello world included, died of SIGSEGV in ld.so: the
  driver linked with `-pie`, the i386 code is position-dependent, and
  ld.so cannot apply text relocations.  i386 programs are now non-PIE.
  *Reproduced.*  `ac391c2b7`.
- [x] **CC-10** The compiler shall build with `-Wall -Wextra -Werror`
  under the host's GCC (16).  Two warnings stopped the native build.
  `1f7c066f2`.
- [x] **CC-51** The compiler shall compile itself correctly.  It
  compiles every one of its sources without an error, but a cc built
  that way aborts (`free(): invalid pointer`) on its first program, at
  -O0 as at -O1.  Swapping one cc-built object at a time into a
  gcc-built cc: `frontend/parser.c`, `frontend/sema.c`,
  `middle/ast2ir.c` and `backend/emit_s.c` each break it.  Step 0 of
  `usr.bin/cc/docs/refactor-plan.md`.  *Reproduced.*
  Most of it was CC-52: with the layout fixed, `sema.c`, `ast2ir.c`
  and `emit_s.c` compile correctly; `parser.c` was CC-54, and the
  self-built cc's own wrong answers were CC-55.  Now a cc built by cc
  (stage 2) passes the codegen, ABI and random checks, and a cc built
  by that one (stage 3) writes byte-identical assembly for all of cc's
  sources: `run_selfhost.sh`.
- [x] **CC-54** A structure passed or returned by value shall be
  copied whole.  The size of each value and of each call argument was
  kept in one byte and clamped at 255, so a larger structure was copied
  in part: `parser.c` passes its 304-byte statement structure by value,
  and the self-built cc freed the garbage in the uncopied tail.  The
  sizes are 32 bits now.  *Reproduced* (both).  `codegen/big_struct.c`.
- [x] **CC-55** Converting a long double to double or float shall round
  it.  On x86-64 there was no conversion at all: the double was the low
  8 bytes of the 80-bit value, part of its mantissa, so `(double)0.5L`
  was -0 -- and the self-built cc, whose emitter turns each floating
  constant into bytes through a long double, wrote every double in data
  wrong.  There is now an explicit operation for it, `CC_SSA_FROUND64`,
  used by casts, initializers, assignments, returns and arguments.
  *Reproduced* (x86-64).  `codegen/ldouble_convert.c`.
- [x] **CC-52** Structure layout shall be the psABI's.  Alignment was
  taken to be the size capped at the pointer size, so on x86-64 a
  `long double` member was aligned to 8, not 16, and every member after
  it was 8 bytes early -- the compiler's own `cc_token_t` was 104 bytes
  where gcc makes it 112, which is what broke the self-built cc.  On
  i386 `long double` was 16 bytes, not 12, and `_Decimal64` and
  `_Decimal128` were under-aligned on both targets.  The four copies
  of the scalar-size table (parser, sema, lowering, emitter) are
  replaced by `frontend/types.c` over a per-target description in
  `common/target.c`.  `float _Complex` and `long double _Complex` still
  take `double _Complex`'s layout: cc has one complex kind (refactor
  step 5).  *Reproduced* (both).  `codegen/struct_layout.c`.
- [x] **CC-53** `offsetof` shall have type `size_t` and step through
  an array member by its element.  It was `long long`, so on i386 it
  passed 8 bytes to `printf("%zu")` and shifted every later argument;
  a subscript stepped by the pointer size, not the element's
  (`offsetof(struct s, v[2])` for `long double v[]` gave 32, not 48);
  and a member of an array element (`offsetof(struct s, a[1].d)`) was
  rejected.  *Reproduced* (both).  `codegen/struct_layout.c`.

## 3. Tests

- [x] **CC-11** The compiler shall have tests that CI runs.  None
  existed: the suite went out in `f2e3fe0d4` and did not come back with
  the sources; CI built cc and never ran what it compiled.  The suite
  is restored (`bc68a0359`), the codegen and random runners added
  (`ad09b201a`, `9ce14449a`) and run by CI (`8999221a4`).
- [ ] **CC-12** CI shall run the whole of `make -C tests/usr.bin/cc`.
  It cannot until the failures in area 6 are gone.

## 4. ABI and other code generation

- [x] **CC-13** On i386, structs shall be passed and returned by value
  as the i386 psABI (and substrate's gcc) does.  A struct return gave
  the address of the callee's own stack copy, which the caller read
  after the stack was reused (`x = -135269504`); a struct argument was
  passed as a pointer.  Both agreed only with cc itself, so calls to
  and from gcc-built code -- libc's `div()` among them -- were wrong.
  Now: a hidden first argument the callee pops (`ret $4`), and the
  struct's bytes copied onto the stack.  *Reproduced* (-m32).
  `codegen/struct_return.c`, `run_codegen_abi.sh`.
- [x] **CC-14** On i386, every argument shall take a multiple of 4 stack
  bytes.  Parameter offsets advanced by each parameter's own size, so
  after a `char` the later parameters were read 3 bytes short: twelve
  mixed arguments summed to 67114532, not 120.  *Reproduced* (-m32).
  `codegen/call_args.c`.
- [x] **CC-15** On i386, converting a double to an integer shall
  truncate toward zero and keep every bit of the integer type.  The x87
  conversion dropped its result: the helper flushed the register state
  after the destination register had been chosen, so `(int)a` gave the
  address of `a`.  It also converted to 32 bits only, so
  `(unsigned)3.9e9` and every `long long` result were wrong, and local
  initializers were never converted to their integer type, so
  `long long x = d * 4` kept 32 bits.  *Reproduced* (-m32).
  `codegen/float_to_int.c`.
- [ ] **CC-16** On i386, `long double` shall be 12 bytes with 64-bit
  precision (the i386 psABI).  It is 16 bytes with double's precision.
  *Reproduced* (-m32).
- [x] **CC-17** A call through an element of an array of function
  pointers shall have the function's return type.  `t[i](6, 7)` was
  typed as a pointer, so `r * 100 + t[0](6, 7)` and `q + t[1](6, 7)`
  were pointer arithmetic: the integer was scaled by 4.  Sema took the
  subscript's `*(t + i)` for `(*fp)(...)` and stripped one pointer level
  too many.  *Reproduced* (all modes).  `codegen/fnptr_call.c`.
- [x] **CC-18** In an array initializer, an element after a designated
  one shall go to the next index.  `{[4] = 40, [1] = 10, 11}` put 11 at
  index 5, not 2: an undesignated element was always appended.
  *Reproduced* (all modes).  `codegen/designated_init.c`.
- [x] **CC-19** `sizeof` of a variable-length array shall be its run-time
  size.  It was rejected: "unsupported sizeof operand in lowering", and
  `sizeof(int[n])` with "sizeof unsupported for this type".
  *Reproduced.*  sizeof of a VLA gives the byte count computed for its
  allocation, and sizeof of a type name with a run-time bound becomes
  `(size_t)n * sizeof (element)`, evaluated where the sizeof is.
  `codegen/vla_sizeof.c` checks eleven sizes against gcc.
- [ ] **CC-66** A variable-length array shall have any number of
  dimensions.  Only one is lowered: `double b[n][3]`, `char c[n][m]` and
  `long d[2][m]` are all rejected ("invalid local array size in
  lowering").  *Reproduced* (both targets).
- [ ] **CC-67** A variable-length array's storage shall be released when
  its block is left.  It is taken with `malloc` (`kmalloc` freestanding)
  and never freed, so a function with `int a[n]` leaks n ints a call: 20000
  calls with a 256 KB array exhaust a 1 GiB address space and the program
  dies of SIGSEGV, where gcc's build (the array on the stack) runs.
  *Reproduced.*
- [ ] **CC-68** `_Complex` and `_Imaginary` objects shall hold both
  parts and support complex arithmetic.  cc carries one part, as a
  double, in a 16-byte object; a 16-byte access is taken for a long
  double, so a complex object read through a pointer (`*pz`) gives 0 on
  both targets and a global one gives 0 at -m64.  Locals read and write
  their first part since CC-23.  *Reproduced.*
- [ ] **CC-69** `_Decimal32`, `_Decimal64` and `_Decimal128` shall be
  decimal floating types in the encoding gcc uses (BID on x86).  cc
  carries all three as a binary double, so a decimal object's bytes are
  not gcc's, decimal rounding is binary rounding, and a value passed to
  or from gcc-built code is wrong.  Sizes and alignments are right.
  *Reproduced* (`_Decimal64 d = 2.0` holds the bytes of the double 2.0).
- [ ] **CC-70** Integers wider than 64 bits shall work: `__int128` on
  x86-64 and `_BitInt(N)` up to a `BITINT_MAXWIDTH` above 64 (gcc's is
  65535), with the C23 `wb`/`uwb` literal suffixes.  The IR's integers
  are 64-bit; since CC-22 cc rejects the wider types instead of
  miscompiling them, and `1wb` does not lex.  *Reproduced.*
- [ ] **CC-20** Where cc runs on i386 and writes x86-64 code (`-m64`),
  the code shall be what a 64-bit-hosted cc writes.  24 of 200 random
  -m64 builds by a `gcc -m32` build of cc are wrong.  *Reproduced.*
- [x] **CC-35** Each arm of `?:` shall be converted to the result's
  type.  An arm narrower than an `unsigned long long` result was moved
  into it unconverted, so on i386 its high word was whatever the stack
  held: `c ? (short)0 : v0` gave `f7d0710000000000`.  Found when local
  initializers began to be converted (CC-15), which moved the garbage.
  *Reproduced* (-m32).  `codegen/ternary_widen.c`.
- [x] **CC-34** `(*fp)(...)` shall call `fp` when `fp`'s function
  returns a pointer.  `int *(*pf)(...)` has the pointer depth of a
  pointer to a function pointer, and lowering keeps the dereference for
  those, so it loads from the function's code and calls that: SIGSEGV.
  `pf(...)` works.  The type system does not record which pointer
  levels belong to a function declarator.  *Reproduced* (all modes).
  Fixed where the callee is a local, a parameter or a global, or an
  element of one (`(*pf)()`, `(**pf)()`, `(*arr[i])()`): those record
  their real type, typedef names included, and a `*` applied to a
  pointer to a function is dropped (`codegen/call_through_deref.c`).
  Structure members record their real type too, so `(*s.f)()` and
  `(*p->f)()` call f (`codegen/member_declarators.c`).
- [ ] **CC-49** A GNU C nested function shall be able to use the
  enclosing function's locals.  `int x = 3; int inner(int y) { return
  x + y; }` gives "use of undeclared identifier: x"; gcc accepts it
  (and needs a trampoline when `inner`'s address escapes).  The suite
  had a test requiring the rejection; it is removed.  *Reproduced.*
- [ ] **CC-56** On i386, `long double` arithmetic shall be done in x87
  extended precision.  The i386 back end keeps every long double value
  as a `double` and computes in double, converting to the 80-bit form
  only where it loads or stores memory, so `1.0L + 0x1p-60L != 1.0L` is
  false under `cc -m32` and true under gcc (and under `cc -m64`).
  Results agree whenever the values fit in a double, which is why the
  suite's long double tests, with values like 0.5, pass.  The fix is a
  back-end change (x87 slots for long double values, as x86-64 has),
  best done with the neutral-IR plan's typed values, where `f80` is a
  type of its own.  *Reproduced* (-m32).
- [x] **CC-50** `sizeof` shall take an abstract function declarator:
  `sizeof(int(void))` (gcc: 1, with a `-Wpointer-arith` warning) gave
  "expected ')' after sizeof type".  *Reproduced.*  Type names are now
  parsed by a real declarator parser into the interned type graph
  (`frontend/ctype.c`) and `sizeof` of one folds to its size, so this,
  `int (*)[7]`, `char (*)[4][5]` and `void (*(*)(int))(void)` all give
  gcc's answer.  `codegen/sizeof_type_names.c`.
- [x] **CC-57** A local multi-dimensional array shall take a brace-nested
  initializer: `int a[2][2] = {{1, 2}, {3, 4}};` inside a function gave
  "initializer list cannot be used as runtime expression in lowering";
  the flat `{1, 2, 3, 4}` worked.  A list of strings for rows of
  characters, `char s[2][6] = {"hello", "world"};`, compiled and filled
  the array with garbage, and the braced `{{"abc"}, {"de"}}` was
  rejected.  *Reproduced* (both targets).  Rows initialized whole --
  braced or by a string -- go through the walk compound literals use,
  which takes brace elision too, and a braced string now counts as its
  list's one element.  `codegen/local_array_init.c` checks nine arrays
  against gcc.  (A `static` local was a separate failure: CC-60.)
- [x] **CC-58** sizeof shall give the size of an array of pointers to
  arrays: for a local `int (*rows[2])[4]`, `sizeof rows` was 64 on x86-64
  (gcc: 16) and 32 on i386 (gcc: 8) -- the legacy type encoding sizes it
  as the array of arrays it cannot tell it from -- and `sizeof` of a
  local `long (*cube)[2][3]` did not compile.  *Reproduced.*  A local's
  declaration now records its real type, and `sizeof` of a local folds
  to that type's size.  `codegen/sizeof_locals.c`.  (Parameters and
  globals still go through the legacy encoding.)
- [x] **CC-59** A structure member declared with a parenthesized
  declarator shall get the type, size and offset gcc gives it.  A member
  `int (*rowp)[4]` -- a pointer to an array -- was laid out as an array
  of four pointers (32 bytes on x86-64, 16 on i386; gcc: 8 and 4), so it
  and every member after it sat at the wrong offsets, breaking the
  layout any other compiler gives the structure.  `int (*fns[2])(int)`
  and `int (*(*gf)(void))(int)` were rejected ("expected ')' in struct
  member declarator"), and a plain `int rows[2][4]` member could not be
  indexed ("dereference requires pointer operand").  *Reproduced* (both
  targets).  Members are now parsed by the real declarator parser and
  record their type; a pointer-to-array member is laid out, read and
  indexed as a pointer.  `codegen/member_declarators.c` checks layout
  and access for twelve member shapes against gcc.
- [x] **CC-60** A `static` local multi-dimensional array with an
  initializer shall hold its initial values.  `static int a[3][4] = {1,
  2, ..., 12};` inside a function -- the usual way to write a lookup
  table -- compiled without a diagnostic, and the program died of
  SIGSEGV on reading `a[2][1]`: the code that filled it on the first
  call took each scalar for a whole row and copied a row from that
  address.  Nested braces did not compile.  *Reproduced* (both targets).
  A static local array of scalars now gets its initializer as data, as
  a file-scope array does, in any brace form; pointer and structure
  element types keep the first-call fill.  `codegen/static_local_tables.c`
  checks ten tables against gcc.
- [x] **CC-61** sizeof shall give gcc's size for an expression built on
  a variable with a nested declarator.  For `int (*rows[2])[4]`,
  `sizeof rows[0]` was 32 on x86-64 (gcc: 8) and `sizeof *rows[0]` 8
  (gcc: 16); for `char grid[2][3][5]`, `sizeof &grid[1]` was 30 (gcc: 8);
  `sizeof (rows + 1)` was 64 (gcc: 8); the same for globals.
  *Reproduced* (both targets).  sizeof folds from the real type of any
  expression whose type follows from its variables' declarations through
  `*`, `&`, subscripts and pointer + integer.
  `codegen/sizeof_expressions.c` checks 25 expressions against gcc.
- [x] **CC-62** Pointer arithmetic shall step by the size of what the
  pointer points to.  For `int (*rows[2])[4]`, `rows + 1` stepped 32
  bytes on x86-64 (gcc: 8), so `rows[1] = p` wrote past the end of
  `rows`; `&rows[0][1]` stepped 8 (gcc: 16); for `char grid[2][3][5]`,
  `&grid[1] + 1` stepped 5 (gcc: 15); `(*grows[1])[1]` on such a global
  died of SIGSEGV.  *Reproduced* (both targets).  The step is taken
  from the operand's real type wherever it is known.
  `codegen/pointer_steps.c` checks 21 expressions against gcc.
- [x] **CC-63** `__builtin_object_size(p, 0)` shall give the bytes left
  in the object p points into.  It gave the size of the expression p:
  for `char dst[8]`, 8 for `dst + 7` (gcc: 1) and `&dst[2]` (gcc: 6), so
  `__builtin___memcpy_chk` let a copy past the end through; 32 for
  `arr[1]` of a `long arr[3][4]` (gcc: 64); the pointer's own size for
  `&x` and, on i386, for `&dst`; 8 for a pointer from `malloc`, where
  gcc answers "unknown", so a checked copy of more than 8 bytes through
  one aborted the program.  *Reproduced* (both targets).  The object and
  the offset into it are found from real types: arrays, `&`, subscripts
  and pointer +/- a constant; any other pointer's object is unknown
  ((size_t)-1, or 0 for modes 2 and 3).  `codegen/builtin_object_size.c`
  checks twelve pointers and a checked copy against gcc -O2.
- [x] **CC-64** A function definition shall take any declarator.  A
  function returning a pointer to an array, `int (*row_of(int))[4] {
  ... }`, was rejected ("expected function return type"), as was one
  returning a pointer to a function returning a function pointer,
  `int (*(*maker(void))(int))(int) { ... }` ("expected ';' after
  declaration"): function definitions had their own declarator parser
  that knew one level of parentheses and skipped what followed them.
  *Reproduced* (both targets).  They are parsed by the real declarator
  parser, which hands the parameter list after the name to the function
  definition, and a call records its function's return type, so
  `(*row_of(1))[2]`, `row_of(2)[0][3]` and `sizeof *row_of(0)` give gcc's
  answers.  `codegen/function_declarators.c` checks eight functions.
- [x] **CC-65** A cast to a pointer to an array shall index and size as
  gcc does.  `((int (*)[4])p)[2][1]` and `(*(int (*)[4])p)[3]` loaded
  from the array instead of taking its address and died of SIGSEGV;
  `sizeof ((int (*)[4])p)[1]` failed ("unsupported sizeof operand in
  lowering").  *Reproduced* (both targets).  A cast records its type,
  which sizeof, pointer steps and `*` follow.  `codegen/cast_declarators.c`
  checks twelve expressions against gcc.

## 5. Driver and predefined macros

- [ ] **CC-21** If a tool the driver runs cannot be executed, then the
  driver shall say so.  When `as` exited 127 (not runnable), cc exited 1
  and printed nothing.  *Reproduced.*
- [x] **CC-48** A warning shall name the file it is about.  `cc -Wall`
  on `native_warn_assign_cond.c` printed `cc:3:2: warning: assignment
  used as condition in if` -- the program's name where the file's
  should be; the same diagnostic made an error by `-Werror` named the
  file.  Both warning paths now print the file, and the suite checks
  the location.  *Reproduced.*
- [x] **CC-22** The compiler shall predefine `__SIZEOF_INT128__` only if
  `__int128` works.  It is predefined for -m64, and a program using
  `__int128` compiles and dies of SIGSEGV.  *Reproduced*; also suite
  commands 87, 823 and 824.  `__int128` was `_BitInt(128)`, and every
  `_BitInt` wider than 64 bits is carried in a 64-bit value, so all of
  them crashed (SIGSEGV on both targets for `unsigned _BitInt(100)`).
  `__SIZEOF_INT128__` is no longer predefined, `__int128`,
  `__int128_t` and `__uint128_t` are rejected ("__int128 is not
  supported"), `__BITINT_MAXWIDTH__` is 64 and a wider `_BitInt` is
  rejected.  Wider integers are CC-70.

## 6. The restored suite's failing commands

Command numbers are those `run_lines` gives the `test` recipe of
`tests/usr.bin/cc/Makefile` at `ad09b201a`; all fail before this audit's
changes as well.  *Suite.*

- [x] **CC-23** `native_c99_complex_decl` shall run successfully (98).
  At -m64 a `double _Complex` local was loaded and stored as a whole 16
  bytes, which the back end takes for a long double: 7.0 was stored as a
  double and read back with `fldt`.  A complex or imaginary local is now
  accessed as the one double cc carries; its slot keeps the full size.
  The general case is CC-68.
- [x] **CC-24** `native_c11_alignas_zero.c` shall compile and run
  (200, 201).  `_Alignas` in a structure member's specifiers was parsed
  and dropped, so `struct { char c; _Alignas(int) char x; }` put x at
  offset 1 and the test's static assertion failed.  The specifiers'
  alignment now applies to each member they declare; member offsets with
  `_Alignas(16)`, `_Alignas(long)` and `_Alignas(0)` match gcc on both
  targets.
- [x] **CC-25** `native_c99_flexible_array_global_init` shall run
  successfully (321).  A static object's flexible array member was sized
  for the elements its initializer gave (GNU extension), but the
  elements themselves were skipped and the tail emitted as zeros.  They
  are now written: scalar elements, structure elements braced or with
  brace elision, and address constants, matching gcc at -m64 and -m32.
  A string (`{3, "abc"}`) was still left zero and sized without its
  terminating null, a braced string (`{2, {"hi"}}`) did not compile, and
  a lone scalar by brace elision (`{1, 5}`) was zero; all three, and wide
  strings, now match gcc too.
- [x] **CC-26** `native_c99_consteval_limits.c` shall compile and run
  (350, 351).  It does, and matches gcc: met since the constant
  evaluators were made one (CC-50).
- [x] **CC-27** `native_gnu_asm_goto.c` shall compile, run, and emit its
  `jz .L…` targets for -m64 and -m32 (497–505).  The test wrote `%l0`
  for its first label with a `+r` output, which gcc rejects; with the
  numbering of CC-38 it says `%l2`, and compiles, runs and emits both
  targets on both targets.
- [x] **CC-28** `run_postc99_c23.sh` shall pass (764).  Two failures:
  `native_c23_bitint_size.c` expected sizes gcc rejects or contradicts
  (a signed `_BitInt(1)`; 5 bytes for `unsigned _BitInt(33)`), and cc's
  own layouts disagreed (sizeof 8 but `_Alignof` 5; 16 bytes for
  `_BitInt(65)` on i386 where the psABI says 12).  One psABI layout now
  serves every phase, and the test checks what gcc reports.  Then
  `native_c23_decimal_sizes.c`: at -m64 a `_Decimal128` local was read
  back as a long double, the CC-23 failure; it is accessed as the double
  cc carries.  The script passes.
- [ ] **CC-29** `run_simd_intrinsics.sh` shall pass (789).
- [x] **CC-30** If an asm input's constraint names the register of an
  early-clobber output, then cc shall reject it
  (`native_bad_gnu_asm_early_clobber_fixed.c`, 832, 833).  sema now
  rejects an input that can only be in the one register (`a`, `b`, `c`,
  `d`, `S`, `D`) an early-clobber output is fixed to, as gcc does
  ("impossible constraints"); a tied (`"0"`) or `r` input, another
  register, or an output without `&` is still accepted, as by gcc.
- [x] **CC-31** ~~If a `noreturn` function has a non-void return type,
  then cc shall reject it~~ -- withdrawn: gcc accepts such a function
  (C11 makes it no constraint violation), so by the rule that gcc and
  clang judge the tests, the test was wrong.  28ad3309e turned it into
  `native_attr_noreturn_nonvoid.c`, which expects it to compile, and it
  does.
- [x] **CC-32** The suite's `__int128` expectations shall match the
  compiler's behaviour once CC-22 is settled (87, 823, 824).  They do:
  the suite expects no `__SIZEOF_INT128__` and the "__int128 is not
  supported" diagnostic, which is what cc now does.

## 7. The restored suite itself

The suite was audited for whether its checks are right and can fail.
How: every runtime test rebuilt with the host's gcc at cc's default
`-std=gnu17` (or the test's own `-std`) and run; the `clang_*` tests
with clang 23; every negative test (`! cc ...`) checked for what it
asserts and passed to gcc with `-pedantic-errors -Werror`; the scripts
and the `grep` checks read.  189 of 205 runtime tests pass under gcc as
they stand.  *Reproduced* unless marked.

- [ ] **CC-36** A runtime test shall pass under gcc, unless it tests an
  extension gcc lacks.  Five assert acceptance of code gcc (and C)
  rejects: `native_c99_ucn_ident.c` (`A` names a basic-set
  character, forbidden by C11 6.4.3p2), `native_c99_wide_literals.c` (a
  wide string assigned to `const char *`),
  `native_gnu_types_choose.c` (an undeclared identifier in the
  unselected `__builtin_choose_expr` branch),
  `native_gnu_static_nonconst_init.c` (a non-constant static
  initializer) and `native_gnu_attr_type_parse.c` (a vector initialized
  from an `int`).
- [ ] **CC-37** A runtime test shall not depend on unspecified
  behaviour.  `native_c99_types.c`, `native_c99_inline_semantics.c` and
  `native_gnu89_inline.c` call a function with only an inline
  definition; whether that uses the inline or an external definition is
  unspecified (C11 6.7.4p7), and gcc's build does not link.
- [x] **CC-38** `native_gnu_asm_goto.c` shall number asm-goto labels as
  GNU C does: after outputs and inputs, so with one `+r` output the
  first label is `%l2`, not `%l0`.  gcc rejects the test; cc fails it
  too (CC-27).  cc numbered labels after the outputs and inputs but
  counted a `+` output once; it now counts it twice, as an output and
  its implicit input, in both the check (sema) and the rendering.  A
  negative case checks that `%l0` is rejected.
- [ ] **CC-39** The default `gnu17` mode shall treat trigraphs as gcc's
  GNU modes do, or the trigraph tests shall name an ISO mode:
  `native_c95_trigraph.c` and `native_c99_phase12_trigraph_splice.c`
  rely on trigraphs in `gnu17`, where gcc ignores them.
- [ ] **CC-40** A `clang_*` test shall pass under clang.  Of five, two
  do (`asm_goto_quirks`, `builtin_aliases`).
  `native_clang_blocks_decl.c` assigns a function to a block pointer,
  which clang rejects -- cc treats blocks as function pointers;
  `native_clang_ext_vector_attr.c` initializes a vector from, and casts
  one to, a scalar, which clang (and gcc) reject;
  `native_clang_attr_placement.c` uses `__attribute__((unused));` as a
  statement, which clang rejects and gcc accepts -- it tests gcc
  behaviour under a clang name.
- [x] **CC-41** A negative test shall check that cc rejected the input
  for the reason named, not merely that it failed.  60 of 68 checked
  only the exit status, so a crash, or a tool that cannot run, passed:
  the `__int128` test passed when `as` could not be executed.  52 now
  live in `negative/CASES.txt` with the diagnostic each must give,
  checked by `run_negative.sh`, which also fails on a signal; three more
  grep their log in the Makefile.  The other five are CC-42.
- [x] **CC-42** A negative test shall expect rejection only of invalid
  code.  Five expect cc to reject valid code:
  `native_bad_gnu_nested_capture.c` (GNU C allows a nested function to
  use the enclosing locals; gcc accepts it), `native_bad_sizeof_void.c`
  (`sizeof(void)` is 1 in GNU modes, the default),
  `native_bad_attr_noreturn_nonvoid.c` (a `noreturn` function may have
  a non-void type), `native_bad_attr_nonnull_empty.c` (gcc warns, does
  not reject) and `native_c90_oldstyle.c` at `-std=c99` (old-style
  definitions are valid until C23).  These record cc's limits as
  requirements: they will fail when cc gains the feature.
  Done: `noreturn_nonvoid` and `nonnull_empty` are now must-compile
  tests (renamed), and cc accepts old-style definitions in every mode,
  as gcc does, while implicit `int` -- which the old-style switch also
  governed -- is allowed only in C89 modes, as gcc 14 and later have
  it.  `sizeof(void)` is 1, as in gcc, with a `-pedantic` warning
  (`native_gnu_sizeof_void.c`, which also checks the error under
  `-pedantic-errors`).  The nested-function test is removed: cc does
  not implement what it tested, and that is CC-49.
- [x] **CC-43** `run_pp_cmd_injection_test.sh` shall run cc.  It ran
  `../../usr.bin/cc/cc` from `tests/usr.bin/cc`, which does not exist,
  and ignored the failure (`|| true`), so it passed without testing
  anything.  It now runs the tree's cc and fails if cc cannot be run.
- [x] **CC-44** `run_pp_fuzz_smoke.sh` shall fail when cc crashes on its
  input, and keep the input that made it.  It ignored every result
  (`|| true`), including SIGSEGV, and its input came from
  `/dev/urandom` and was discarded.  Inputs now come from fixed seeds,
  a signal or a hang fails the test, and the input is kept.
- [x] **CC-45** If `test_ast2ir.c` does not build, then the suite shall
  fail.  Its build ended in `|| true`, and the next command ran
  `./test_ast2ir_bin` -- a stale binary from an earlier run passed.
  The binary is now removed first and the build may not fail.
- [ ] **CC-46** A check of generated assembly shall show the property
  under test.  30 of the 190 `grep` checks only look for `.code32` in
  `-m32 -S` output, which shows the flag was read, not that the code is
  right: the -m32 output of those tests is never assembled or run.
  Others look for `jne`, `movl` or `jmp`, which almost any function
  contains.  *Traced.*
- [ ] **CC-47** The suite shall use a private temporary directory.  It
  writes 310 fixed `/tmp/cc_*` paths, so two runs (two CI jobs on one
  host, or two users) overwrite each other's files.  *Traced.*

## 8. Documentation

- [x] **CC-33** `usr.bin/cc/README.md` shall describe the compiler as it
  is.  It described "phase-9", a scalar-only subset, `-E` via the system
  `cpp`, and `--bootstrap-gcc` as the way to compile ordinary C.  It is
  rewritten from the code: the pipeline, the languages and targets, how
  tools are found, how correctness is checked, what is known missing.
  The phase roadmap (`docs/ROADMAP.md`, "Phase 0 (current)") is gone,
  superseded by the two plans; the language task list moved to
  `docs/language-tasklist.md`.

---

70 requirements: 52 met, 18 open.

The structural refactor and the target-neutral IR are planned in
`usr.bin/cc/docs/refactor-plan.md` and `usr.bin/cc/docs/neutral-ir-plan.md`.
