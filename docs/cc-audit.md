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
| 3. Tests | 2 | 2 |
| 4. ABI and other code generation | 37 | 37 |
| 5. Driver and predefined macros | 3 | 3 |
| 6. The restored suite's failing commands | 10 | 10 |
| 7. The restored suite itself | 12 | 12 |
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
- [x] **CC-12** CI shall run the whole of `make -C tests/usr.bin/cc`.
  It cannot until the failures in area 6 are gone.  They are: all 855
  commands pass, and `make -C tests/usr.bin/cc` passes locally (about 4
  minutes).  The `cc-codegen` job of `substrate-toolchain.yml` now runs
  it after the self-host check, with the cc it built.

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
- [x] **CC-16** On i386, `long double` shall be 12 bytes with 64-bit
  precision (the i386 psABI).  It is 16 bytes with double's precision.
  *Reproduced* (-m32).  The layout was already right (12 bytes, 4-aligned,
  a structure's members placed as gcc places them); the precision is
  CC-56's fix.
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
- [x] **CC-66** A variable-length array shall have any number of
  dimensions.  Only one is lowered: `double b[n][3]`, `char c[n][m]` and
  `long d[2][m]` are all rejected ("invalid local array size in
  lowering").  *Reproduced* (both targets).  The parser now keeps every
  dimension's bound, not only a lone one, and the declaration computes a
  stride per dimension (the size of one of its elements) along with the
  total; subscripts, pointer steps and `sizeof` on the array or on any
  element of it use those strides.  `codegen/vla_multidim.c` covers the
  three shapes above, three dimensions, rows passed as pointers,
  addresses of elements, structure elements, an array of pointers and a
  VLA in a loop, matching gcc at both widths.
- [x] **CC-67** A variable-length array's storage shall be released when
  its block is left.  It is taken with `malloc` (`kmalloc` freestanding)
  and never freed, so a function with `int a[n]` leaks n ints a call: 20000
  calls with a 256 KB array exhaust a 1 GiB address space and the program
  dies of SIGSEGV, where gcc's build (the array on the stack) runs.
  *Reproduced.*  VLAs and `alloca` now take their storage off the stack
  (a new IR operation, DYNALLOC), 16-aligned, so it goes when the function
  returns.  A VLA's declaration also records the stack pointer it found
  and, when it runs again (a loop), first puts the stack pointer back
  there, so a VLA in a loop reuses its storage as gcc's does; `alloca`
  storage lasts until the function returns, as it should.  The repro runs,
  and `codegen/vla_stack.c` checks that a VLA in a loop stays in place.
- [x] **CC-68** `_Complex` and `_Imaginary` objects shall hold both
  parts and support complex arithmetic.  cc carries one part, as a
  double, in a 16-byte object; a 16-byte access is taken for a long
  double, so a complex object read through a pointer (`*pz`) gives 0 on
  both targets and a global one gives 0 at -m64.  Locals read and write
  their first part since CC-23.  *Reproduced.*  *Layout* (refactor
  step 5): a complex or imaginary type's element -- float, double or
  long double -- is in its interned type, and sizes, alignment,
  `_Alignof` and structure layout come from it (`float _Complex` was 16
  bytes, gcc's 8; `codegen/complex_layout.c`).  *Values:* in the legacy
  encoding a complex type is the structure `{ T __re, __im; }` standing
  for it, so objects, copies, calls and returns are a structure's; sema
  makes every real/complex conversion an explicit cast (assignment,
  initialization, arguments, returns, casts, conditions and `!`), and
  lowering does the arithmetic part by part in each operand's type:
  + - * on every pairing of float, double and long double complex with
  each other and with real and integer operands, a real operand kept
  real (Annex G), division by Smith's method in libgcc's order (float
  parts in double, as `__divsc3` does), `==`/`!=`, `~` as conjugation.
  GNU `__real__`/`__imag__` (also as lvalues), imaginary constants
  (`2.0i`, `1.0fi`, `<complex.h>`'s `I`), `__builtin_complex`, and
  constant initializers of static complex objects and arrays are
  supported.  The psABI special cases are kept: x86-64 returns
  `long double _Complex` in `%st0`/`%st1`, i386 returns `float _Complex`
  in `%eax`:`%edx`.  Two parser bugs went with it: `__imag__` was lexed
  as the `_Imaginary` keyword, and the declaration probe recorded its
  own complex structures over the real ones.  `codegen/complex_arith.c`
  matches gcc to the bit at both widths and both optimization levels;
  `codegen_abi/` checks the three complex types against gcc both ways.
  Not done: `_Imaginary` as its own type (gcc does not support it
  either), complex integer types (`3i` is a double imaginary), and
  libgcc's rescaling of huge or tiny divisors.
- [x] **CC-69** `_Decimal32`, `_Decimal64` and `_Decimal128` shall be
  decimal floating types in the encoding gcc uses (BID on x86).  cc
  carries all three as a binary double, so a decimal object's bytes are
  not gcc's, decimal rounding is binary rounding, and a value passed to
  or from gcc-built code is wrong.  Sizes and alignments are right.
  *Reproduced* (`_Decimal64 d = 2.0` holds the bytes of the double 2.0).
  Each type is now a hidden structure holding its BID encoding.
  `frontend/decimal.c` encodes constants: a `df`/`dd`/`dl` literal's
  coefficient and exponent as written, rounded half to even to the
  type's precision, clamped or overflowing to infinity at its exponent
  range; an integer exactly; a binary floating constant as gcc folds one
  (its exact decimal value, trailing zeros dropped but one digit kept
  after the point, so `2.0` is 20e-1 and `0.1` its 16 or 34 rounded
  digits).  Sema folds constant conversions and negations that way,
  converts integer operands to the decimal type and the narrower decimal
  operand to the wider, and rejects binary floating operands and integer
  operators (C23 6.3.1.8).  Arithmetic, comparisons and every conversion
  call libgcc's `__bid_*` functions, as gcc does; `++`/`--`, compound
  assignment, truth values and static initializers (of scalars, arrays
  and members) work.  Decimals are passed and returned as gcc does: in
  xmm registers on x86-64 (`_Decimal128` whole in one); on i386
  `_Decimal32` as an int and in `%eax`, `_Decimal64` on the stack and in
  `%eax:%edx`, `_Decimal128` on the stack 16-aligned and in memory.  The
  keywords are taken in every language mode, as gcc takes them.  Where cc
  links against glibc it now names the dynamic linker `AS_NEEDED`, as
  glibc's `libc.so` script does, since libgcc's decimal conversions use
  TLS.  `codegen/decimal_ops.c` matches gcc to the bit at both widths
  and levels; `codegen_abi/` passes the three types both ways.  Not
  done: constant folding of decimal arithmetic (a static initializer
  like `1.5DD + 2` is rejected), and a decimal program for substrate
  needs a libgcc built with decimal float, which substrate's toolchain
  does not have.
- [x] **CC-70** Integers wider than 64 bits shall work: `__int128` on
  x86-64 and `_BitInt(N)` up to a `BITINT_MAXWIDTH` above 64 (gcc's is
  65535), with the C23 `wb`/`uwb` literal suffixes.  The IR's integers
  are 64-bit; since CC-22 cc rejects the wider types instead of
  miscompiling them, and `1wb` does not lex.  *Reproduced.*
  A wide integer is now a hidden structure of pointer-sized limbs, as
  gcc lays it out (`__int128` 16-aligned; `_BitInt` limbs 8 bytes on
  x86-64, 4 on i386), and is passed and returned as that structure,
  which is the psABI's rule for it.  Sema converts the operands of a
  wide operator to their common type; lowering does `+ - & | ^`, shifts
  and comparisons limb by limb, re-extends the top limb after each
  operation, and calls libgcc's `__mulbitint3`, `__divmodbitint4`,
  `__floatbitint*` and `__fix*bitint` for `* / %` and the floating
  conversions.  `++`/`--`, compound assignment, `!`, `&&`, `||` and
  conditions work; static initializers are folded at the type's width.
  `wb` constants of any width lex, with the C23 type (`sizeof(5wb)` is
  1).  `__SIZEOF_INT128__` is 16 and `__int128` accepted on x86-64
  only; `__BITINT_MAXWIDTH__` is 65535.  `codegen/wide_int.c` matches
  gcc to the bit at both widths and both optimization levels;
  `codegen_abi/` passes `_BitInt(100)`, `(200)`, `(130)` and `__int128`
  both ways.  A small `_BitInt` shifted keeps its width.
- [x] **CC-71** GNU and clang vector types (`vector_size`,
  `ext_vector_type`) shall work: their size and alignment, brace
  initialization, subscripts and element-wise operators.  cc parsed the
  attributes and laid the type out as its element -- `sizeof` of a
  `vector_size(16)` int vector was 4 -- so vector code was silently
  wrong; since CC-40 cc rejects the attributes and `__has_attribute`
  reports them absent.  cc's own `<xmmintrin.h>` uses structs and is
  unaffected.  *Reproduced.*
  A vector is now a hidden structure `{ T __vec[n]; }` aligned to its
  size up to 16, as gcc lays one out (`ext_vector_type(n)` rounds its
  size up to a power of two, as clang does); the attribute applies
  before or after the declarator, to typedefs, variables and members.
  `v[i]` indexes the element array and is an lvalue.  Every operator
  works element by element -- `+ - * / %`, the bitwise ones, shifts,
  unary `- ~ +`, `++`/`--` and compound assignment -- with a scalar
  operand used for every element; comparisons give -1 or 0 in the
  signed vector of the element's size.  A cast between vectors, or to or
  from an integer, of one size keeps the bits.  `typeof` of a vector
  expression is its type.  Vectors are passed and returned as gcc does:
  on x86-64 16 bytes whole in one xmm register, 8 SSE, 4 INTEGER, wider
  in memory aligned to their size; on i386 the first three 16-byte
  vectors in `%xmm0`-`%xmm2` and 8-byte ones in `%mm0`-`%mm2` (with
  `emms` after), a 4-byte one as an int, the rest on the stack aligned
  to their size, and the return in `%xmm0`, `%mm0` or `%eax`.  Stack
  temporaries of 16 bytes or more are 16-aligned now, as gcc-built code
  may `movaps` through a pointer to one.  `codegen/vector_ops.c`
  matches gcc at both widths and levels, `codegen_abi/` passes vectors
  of 4 to 64 bytes both ways, and `ext_vector_type` matches clang.  Not
  done: clang's `.xyzw` swizzles, `__builtin_shuffle` and
  `__builtin_convertvector`, and vector conditions in `?:`.
- [x] **CC-72** A pointer to a variable-length array, and a parameter
  declared as one (`int a[n][m]`, adjusted to `int (*a)[m]`), shall step
  by the array's run-time size.  cc steps by a constant computed with
  each run-time bound taken as 1, silently: `int (*p)[m] = a; p[1][2]`
  reads the wrong element, and `int get(int n, int m, int a[n][m])`
  returns `a[i][j]` from the wrong row (5 for gcc's 13).
  `sizeof(int[n][m])` is rejected ("sizeof unsupported for this type").
  *Reproduced* (both targets; the parameter case predates CC-66).
  A parameter's declarator dropped its bounds; it now keeps them, the
  parameter owns them, and at function entry the strides are computed
  from the parameters before it.  A local pointer to a VLA gets its
  strides at its declaration.  A pointer's first stride is the size of
  the array it points to, so subscripts, `+`, `-`, `++`, `--`, the
  difference of two such pointers and `sizeof *p` all use run-time
  sizes.  `sizeof` of a type name with run-time bounds is the product of
  every level's bound and the element size.  `codegen/vla_pointers.c`
  covers these, matching gcc at both widths.
- [x] **CC-73** The address of a GNU nested function that uses the
  enclosing function's variables shall be usable as a function pointer
  (gcc builds a trampoline on the stack that loads the static chain).
  cc passes those variables' addresses as a hidden first argument
  (CC-49), which a call through a pointer would not supply, so it
  rejects taking such a function's address: "the address of a nested
  function that uses the enclosing function's variables cannot be taken
  (it would need a trampoline)".  A nested function that uses none of
  them is an ordinary function and its address works.  The fix needs a
  trampoline (and an executable stack, or a page of trampolines) and a
  static chain register the nested function reads instead of the hidden
  argument.  A nested function using a variable-length array of the
  enclosing function is also rejected.  *Reproduced.*  Done as gcc does
  it: the nested function reads the array from the static chain
  register (`%r10` on x86-64, `%ecx` on i386) at entry (a CHAIN
  instruction), a direct call loads it there, and taking the function's
  address builds a trampoline in the caller's frame -- `movabs $env,
  %r10; movabs $fn, %r11; jmp *%r11`, or `movl $env, %ecx; movl $fn,
  %eax; jmp *%eax` -- whose address is the function pointer.  The
  object is marked as needing an executable stack (`.note.GNU-stack`
  "x"), as gcc's is.  `codegen/nested_trampoline.c` calls through a
  pointer and passes the function to qsort and to a callback, matching
  gcc at both widths.  A variable-length array of the enclosing function
  is still rejected in a nested function (its strides are values of the
  enclosing function).
- [x] **CC-74** On x86-64, a structure of 16 bytes or fewer shall be
  passed and returned by the classes of its eightbytes (psABI 3.2.3):
  one of floats and doubles in xmm registers, a mixed one in both kinds,
  one holding a long double or packed in memory, a lone long double
  returned in `%st0`.  cc put every such structure in general registers,
  so `struct { double x, y; }`, `{ float x, y; }`, `{ double d; long l; }`
  and the like came out as garbage in calls to and from gcc-built code,
  both ways (`{1.5, 2.0} + {0.25, 4.0}` gave `1.4e+161 2.3e-129`).  The
  tail of a 3-, 5-, 6- or 7-byte eightbyte was also moved with the next
  smaller move, losing its last bytes.  *Reproduced* (found by CC-68's
  cross-compiler test).  The lowering classifies each aggregate from its
  members' types (`abi64_aggregate_class()`) and the IR carries the
  classes in the argument's ABI byte and on a CALL or RET
  (`agg_class`); the emitter puts each eightbyte in the next register of
  its class, and odd tails a byte at a time.  `codegen_abi/` now passes
  nine structure shapes, more SSE arguments than xmm registers, and the
  three complex types, cc against gcc both ways at both widths.
- [x] **CC-75** On i386, the stack shall be 16-byte aligned at every call
  (the psABI as gcc and glibc assume it).  cc's frames left `%esp` 8
  bytes off and calls pushed their arguments unpadded, so gcc-built code
  that keeps a 16-byte value in its frame died: `movdqa` in a callee
  copying a 16-byte structure faulted.  *Reproduced* (CC-74's test, -m32).
  Frames are now 8 more than a multiple of 16, which leaves the body's
  `%esp` aligned after the return address and saved `%ebp`, and each
  call pads its arguments to a multiple of 16 first.
- [x] **CC-76** A prototyped `float` parameter or return shall be single
  precision across the call: in the low 32 bits of an xmm register on
  x86-64, in a 4-byte stack slot on i386, as gcc passes it.  cc passed and
  returned the double it holds a float in, so `crealf()`, `sinf()` and
  every other float function of gcc-built code, called from cc or calling
  cc's, got garbage: `fl(1.5f, 2.0, 0.25f)` gave `5.3e-315` (x86-64) and
  `2` (i386) for 5.25.  *Reproduced* (both ways, both widths).  The IR has
  an ABI kind for it, `CC_CALL_ARG_ABI_FLOAT`, on arguments, parameters
  and returns, converted at the boundary (`cvtsd2ss`/`cvtss2sd`,
  `fstps`/`flds`); an old-style definition's float parameters are still
  promoted to double, as C says.  `codegen_abi/`'s `lib_fl` and
  `lib_fl10` (ten floats, some on the stack).
- [x] **CC-77** On x86-64, storing an integer or double value to a long
  double member or through a long double pointer shall convert it.  The
  store read the value's 8-byte slot as a 10-byte long double, so
  `struct { long double a, b; } m = {1, 2.5}` held `1.7e-4932 1.7e-4932`.
  Local and global scalars were right.  *Reproduced* (-m64).  The
  assignment and the structure initializer now widen the value first.
  `codegen/ldouble_convert.c`.
- [x] **CC-78** A floating constant with an `f` suffix shall be the float
  nearest its digits.  cc kept the double, so `0.1f` widened to double
  was `0.10000000000000001` (gcc: `0.10000000149011612`) and `x == 0.1f`
  was false for `float x = 0.1f`.  *Reproduced* (both widths).  The
  lexer converts it with `strtof`.  `codegen/float_literal.c`.
- [x] **CC-79** A floating value of 2^63 or more converted to
  `unsigned long long` shall be that value.  `cvttsd2si` and `fistpll`
  convert to a signed integer, so every such value became 2^63
  (`12345678901234567891.0` gave 9223372036854775808), at run time, in
  the optimizer's folding and in static initializers.  *Reproduced* (both
  widths).  The conversion compares with 2^63 and converts the excess
  with the top bit set back, as gcc does; the folders convert through
  `unsigned long long`, and the unsignedness stays on the conversion when
  a later cast relabels its value (`(long long)(unsigned long long)d`).
  `codegen/float_to_u64.c`.
- [x] **CC-80** On i386, a conditional expression of structure type shall
  give the structure.  Its result -- the address of whichever arm was
  taken -- was given the structure's size as its value size, so a
  16-byte structure's address was handled as a 64-bit pair, and copying
  the result (`struct s p = c ? g : l;`) passed `memcpy` a garbled source
  and died of SIGSEGV.  *Reproduced* (-m32).  The result is now
  pointer-sized for any aggregate.  `codegen/struct_ternary.c`.
- [x] **CC-81** A value converted to `_BitInt(N)`, N at most 64, shall
  keep its low N bits sign-extended, and one converted to
  `unsigned _BitInt(N)` its low N bits zero-extended.  Constants wrapped
  at the type's size in bytes, run-time values not at all, and
  `unsigned _BitInt(N)` was signed throughout: the legacy type encoding
  kept a `_BitInt`'s width and not its sign, so `(_BitInt(7))100` was
  100, not -28, and `(unsigned _BitInt(12))-1 >> 4` was -1, not 255.
  *Reproduced* (both targets).  The parser records the precise type,
  the constant evaluator and sema type bit-precise arithmetic by the
  rules on interned types (the parser marks `-x`, `+x` and `~x`, which
  it spells as binary operators, so they keep the operand's type), and
  lowering wraps to N bits wherever a value is converted.
  `codegen/const_expr.c`, `codegen/bitint_narrow.c`.
- [x] **CC-82** A universal character name in a string literal shall be
  one character: its UTF-8 bytes in a narrow literal, one element in a
  wide one, a surrogate pair in `u""` beyond the BMP.  A narrow literal
  dropped the backslash -- `"é"` held the five bytes `u00e9`, not
  `c3 a9` -- and the size of a literal counted each hex digit of a
  `\u`/`\U` as a character, so `sizeof(u"\U0001F600")` was 4 and the
  type `typeof(u"\U0001F600")` 20 bytes, not 6.  *Reproduced* (both
  targets).  The parser encodes the name in UTF-8; one counter,
  `cc_string_literal_units()`, measures every literal, and sema gives a
  literal its array type from it.  `codegen/wide_string_literals.c`.
- [x] **CC-83** `*p` and `++*p` / `--*p` through a pointer to a
  bit-precise object shall read and write the object's bytes and no
  more.  They used a word, the size the legacy encoding gives every
  `_BitInt`: `(*p)++` on a `_BitInt(7)` in a structure overwrote the
  bytes after it.  *Reproduced* (both targets).  The load and the
  update take the size of the value's interned type.
  `codegen/bitint_narrow.c`.
- [x] **CC-84** A GNU cast to union type shall store the operand in
  the member of its own type, and an operand of a type no member has
  shall be rejected, as gcc does ("cast to union type from type not
  present in union").  cc took the first member the value would
  *convert* to: `(union { int i; double d; })2.5` stored the int 2, a
  `long long` went into an `int` member, and `(union u)(short)1` or
  `(union u)0` with no `int` member were accepted.  *Reproduced* (both
  targets).  Sema and lowering share one rule,
  `cc_ct_union_cast_member()`, which compares the unqualified interned
  types.  `codegen/union_cast.c`, `native_bad_union_cast_type.c`.
- [x] **CC-85** A string literal shall initialize an array of any
  character type, signed char included (C11 6.7.9p14).  Every check that
  admits a string into an array named only char and unsigned char, so a
  `signed char` structure member initialized from a string was rejected
  ("invalid string initializer for fixed-size array struct member").
  *Reproduced* (both targets).  One predicate, `cc_ct_is_character()`,
  is used at all of them.  `codegen/schar_string_init.c`.
- [x] **CC-86** A file-scope pointer initialized from a compound literal
  -- an array one decaying, `static char *p = (char[]){"lit"};` or
  `(int[]){1, 2}`, or the address of a scalar or array one,
  `&(int){5}` -- shall be accepted: the literal has static storage and
  its address is an address constant (C11 6.5.2.5p5, 6.6p9), as gcc has
  it.  cc accepted only `&(struct S){...}` there ("unsupported global
  initializer for p").  *Reproduced* (both targets).  The parser hoists
  a file-scope array literal, and the operand of a file-scope `&`, into
  an anonymous static global initialized by the literal's braces and
  refers to it by name; lowering's own `&(struct S){...}` path is gone.
  (A literal inside a file-scope `typeof` or `sizeof` becomes an unused
  static object as well.)  `codegen/file_scope_compound_literal.c`.
- [ ] **CC-90** A `constexpr` object shall be usable in an integer
  constant expression (C23 6.6p7): `_Static_assert(N == 4, "")` and
  `case N - 2:` are rejected ("static assertion condition must be an
  integer constant", "case label must be an integer constant
  expression"), though `int a[N]` works.  *Reproduced.*
- [ ] **CC-91** The constraints on `constexpr` (C23 6.7.1, 6.7.10)
  shall be diagnosed, as gcc does: assigning to one, initializing one
  from a non-constant, and an initializer not representable in its type
  (`constexpr unsigned char c = 300;` only draws an assembler truncation
  warning and yields 44) are all accepted.  *Reproduced.*
- [ ] **CC-92** `nullptr` shall convert only to a pointer, `bool` or
  `nullptr_t` (C23 6.3.2.4): `int n = nullptr;` is accepted; gcc
  rejects it.  *Reproduced.*
- [ ] **CC-93** A static local array of integers wider than 64 bits
  shall hold the whole value of each initializer (C11 6.7.9p10):
  `static unsigned __int128 u[2] = {5, (unsigned __int128)1 << 100};`
  and `static _BitInt(128) b[2] = {1, 0x123456789abcdef0123wb};` inside
  a function lose each element's upper 64 bits (`u[1] >> 96` is 0, gcc
  16).  File-scope arrays and scalars are laid out right.  *Reproduced*
  (-m64).
- [x] **CC-94** An array of pointers to arrays shall hold pointers (C11
  6.7.6.2): for `int g[2][4]; int (*rows[2])[4] = {&g[0], &g[1]};` cc
  laid `rows` out at the size of the 2x4 array (64 bytes at -m64, gcc
  16) and read `rows[i]` as the element's address instead of loading
  the pointer in it, so `*rows[1]` was not `g[1]`.  *Reproduced* (both
  targets).  Lowering decides whether a subscript or `*` yields an array
  by the expression's interned type, and gives an array global its size
  from its interned type, which the emitter pads to.
  `native_array_of_array_pointers_size.c`.
- [ ] **CC-95** A `_Float16` object shall hold the value it is
  initialized with, and `f16` shall be a floating suffix (C23 7.22,
  Annex H): `_Float16 h = 1.5f;` at file scope is emitted as the integer
  1 (`h` reads 5.96e-08; gcc 1.5), and `1.5f16` is rejected ("expected
  ';' after declaration").  *Reproduced* (-m64).
- [x] **CC-96** An `_Atomic` or `_BitInt` object at file scope shall be
  as big as its type (C11 6.2.5p27, C23 6.2.6.1): cc gave every such
  object the pointer size, so `_Atomic short` and `_BitInt(17)` took 16
  bytes of .data at -m64 (gcc 8).  *Reproduced.*  Lowering hands the
  emitter each global's layout -- size, element size, what its data is --
  from its interned type, and the emitter no longer reads the legacy
  type.  `native_atomic_bitint_global_size.c`.
- [x] **CC-97** The difference of two pointers shall have type
  `ptrdiff_t` (C11 6.5.6p9), `long` on x86-64: cc typed it `int` on both
  targets, so `sizeof (p - q)` was 4 at -m64 (gcc 8) and the difference
  was cut to 32 bits and sign-extended back.  *Reproduced* (-m64).
  Sema types the difference as the target's `ptrdiff_t`.
  `native_pointer_difference_type.c`.
- [x] **CC-98** The arms of `?:` shall be both arithmetic, both
  structures of one type, both void, or pointers that match, a pointer
  and a null pointer constant (C11 6.5.15p3): cc accepted a pointer and
  any other integer, `c ? p : 1`, and typed the result `int` (gcc 16
  rejects it: "pointer/integer type mismatch").  *Reproduced.*  The
  conditional is typed on its arms' interned types and the mismatch is
  an error.  `native_bad_ternary_ptr_int.c` (negative).
- [ ] **CC-99** A bit-field shall be allocated as the System V psABI
  (and gcc) allocate it: in the storage unit of its declared type that
  holds the next free bit, which may be one an earlier non-bit-field
  member already uses.  cc always opens a fresh unit aligned to the
  bit-field's type, so `struct { char a; int b : 3; short e; }` is 12
  bytes with `e` at 8 where gcc makes it 4 with `e` at 2 -- the layout
  of every such structure disagrees with code gcc compiles.
  *Reproduced* (both targets); found while checking member layout for
  the interned-type refactor, and present before it.
- [ ] **CC-100** A type qualifier among the declaration specifiers shall
  qualify the type they give (C11 6.7.3): cc keeps `const` and `volatile`
  there as storage bits beside the type, so `const int *` is a pointer to
  plain `int` wherever the type itself is compared --
  `__builtin_types_compatible_p(int *, const int *)` is 1 (gcc 0).
  *Reproduced* (both targets); present before the interned-type
  refactor.
- [ ] **CC-101** GNU attributes shall be accepted between `enum` and its
  tag, as gcc accepts them: `enum __attribute__((packed)) e { A };` is
  rejected ("expected ';' after declaration"); the attribute after the
  closing brace works.  *Reproduced*; present before the interned-type
  refactor.
- [x] **CC-87** `&` shall apply to a compound literal of any type, which
  is an lvalue (C11 6.5.2.5p4).  cc took the address of structure
  literals only: `&(int){5}` and `&(int[2]){7, 8}` were rejected
  ("address-of currently requires an identifier or member lvalue").
  *Reproduced* (both targets).  Sema recognises the literal before its
  check turns a scalar one into a cast of its value, and lowering puts a
  scalar literal in storage of its own.  `codegen/compound_literal_address.c`.
- [x] **CC-88** A compound literal of array type with no bound,
  `(T[]){...}`, shall take its bound from its initializer (C11
  6.5.2.5p4, 6.7.9p22).  cc left its type incomplete, so
  `sizeof((int[]){1, 2, 3})` was rejected ("unsupported sizeof operand
  in lowering").  *Reproduced* (both targets).  The parser completes the
  literal's type from the list as it parses it; the bound inference also
  takes a signed char array from a string, as CC-85 requires.
  `codegen/compound_literal_bound.c`.
- [x] **CC-89** `(*q)[i]` through a file-scope pointer to an array,
  `static int (*q)[2] = &arr;`, shall read the array's element.  cc read
  the pointer's own address instead (`(*q)[0]` gave q's address,
  `(*q)[1]` 0, and `l = q` stored q's address): the legacy encoding
  gives `int (*q)[2]` the dimensions of `int q[2]`, and lowering took
  either for an array used by its address.  *Reproduced* (both targets).
  Lowering asks the identifier's interned type, its declaration's,
  whether it is an array.  `codegen/global_array_pointer.c`.
- [x] **CC-20** Where cc runs on i386 and writes x86-64 code (`-m64`),
  the code shall be what a 64-bit-hosted cc writes.  24 of 200 random
  -m64 builds by a `gcc -m32` build of cc are wrong.  *Reproduced.*
  Three places held a target value in a host `long`, 32 bits on i386:
  an unsigned mask converted through `(long)` (so `0xffffffff` became
  -1), switch case values (so `case 0xffffffffu` matched -1), and the
  value-size clamp, `size > (long)UINT32_MAX`, which on i386 compares
  against -1 and so clamped every size.  Now 0 of 200 random -m64
  programs are wrong, and a 32-bit-hosted cc writes the same assembly as
  a 64-bit-hosted one for the whole corpus at -m64 and -m32;
  `run_crosshost.sh` builds both and checks that for `codegen/`.
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
- [x] **CC-49** A GNU C nested function shall be able to use the
  enclosing function's locals.  `int x = 3; int inner(int y) { return
  x + y; }` gives "use of undeclared identifier: x"; gcc accepts it
  (and needs a trampoline when `inner`'s address escapes).  The suite
  had a test requiring the rejection; it is removed.  *Reproduced.*
  A nested function records where it was defined; it sees the enclosing
  function's parameters and the top-level declarations before it (and,
  nested further, what its own enclosing function sees), which sema puts
  in an outer scope.  One that uses any of them, or calls a nested
  function that does, is given an array of their addresses, built at
  each call from the caller's scope, in the static chain register
  (`%r10`, `%ecx`; since CC-73, before it a hidden first argument).  Reads and
  writes go through those addresses, so they reach the enclosing
  function's objects.  `codegen/nested_functions.c` covers scalars,
  structures, arrays, a static local, a parameter, recursion, calls
  between nested functions, a function nested in a nested function, and
  a non-capturing nested comparator passed to `qsort`, matching gcc at
  both widths.  Taking the address of a nested function that uses the
  enclosing function's variables is CC-73.
- [x] **CC-56** On i386, `long double` arithmetic shall be done in x87
  extended precision.  The i386 back end keeps every long double value
  as a `double` and computes in double, converting to the 80-bit form
  only where it loads or stores memory, so `1.0L + 0x1p-60L != 1.0L` is
  false under `cc -m32` and true under gcc (and under `cc -m64`).
  Results agree whenever the values fit in a double, which is why the
  suite's long double tests, with values like 0.5, pass.  The fix is a
  back-end change (x87 slots for long double values, as x86-64 has),
  best done with the neutral-IR plan's typed values, where `f80` is a
  type of its own.  *Reproduced* (-m32).  Done as x86-64 does it: a
  function with a long double value gets 16-byte slots, a long double
  value is kept in its slot in the 80-bit form (fldt/fstpt), and every
  instruction that touches one runs on the x87 even under
  `-mfpmath=sse`: arithmetic (both operands on the stack, as no x87
  operation takes an 80-bit memory operand), compares, conversions,
  moves, loads and stores, parameters, arguments, returns, and
  constants (written from the full long double value).  A 64-bit
  integer converted to floating, which was truncated to its low 32 bits
  on i386 (`(double)1234567890123456789LL` was 2112454933, also for
  double), is converted whole (`fildll`).  `codegen/ldouble_precision.c`
  matches gcc at -m32 and -m64, x87 and SSE.
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

- [x] **CC-21** If a tool the driver runs cannot be executed, then the
  driver shall say so.  When `as` exited 127 (not runnable), cc exited 1
  and printed nothing.  *Reproduced.*  The forked child now reports a
  failed exec as gcc does (`cc: fatal error: cannot execute '/x/as': No
  such file or directory`), the driver reports a tool killed by a signal
  and a failed fork, and cc(1) says so.  A suite check runs cc with
  `AS` naming a missing program.
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
- [x] **CC-29** `run_simd_intrinsics.sh` shall pass (789).  `-mavx`
  did not define `__AVX__`: the preprocessor read the target flags in
  order, and the driver appends its own `-m64`, which reset the ISA
  features.  It now reads `-m32`/`-m64`/`-march` first and the feature
  flags after, so their order does not matter, as with gcc; `-m64` keeps
  an explicit `-march`'s features (`-march=x86-64-v3` gave no AVX
  either); AVX2 implies AVX at the end, so `-mavx2 -mno-avx2` leaves
  neither; and any x86-64 `-march` may add AVX by flag.  The x86-64
  `__AVX__`/`__AVX2__` results match gcc for 16 flag orders, three of
  them now in the script.
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

- [x] **CC-36** A runtime test shall pass under gcc, unless it tests an
  extension gcc lacks.  Five assert acceptance of code gcc (and C)
  rejects: `native_c99_ucn_ident.c` (`A` names a basic-set
  character, forbidden by C11 6.4.3p2), `native_c99_wide_literals.c` (a
  wide string assigned to `const char *`),
  `native_gnu_types_choose.c` (an undeclared identifier in the
  unselected `__builtin_choose_expr` branch),
  `native_gnu_static_nonconst_init.c` (a non-constant static
  initializer) and `native_gnu_attr_type_parse.c` (a vector initialized
  from an `int`).  Three were test bugs, now valid C: the wide literal
  goes to a `const wchar_t *` (which showed cc emitted every wide
  literal as narrow bytes -- fixed), the unselected `choose_expr` arms
  name a declared function, the vector is brace-initialized.  Two were cc
  accepting what C forbids: cc now rejects a UCN for a basic-set
  character in an identifier (the test uses `\u00e9` and
  `\U000003b1`, a negative case `\u0041`) and a static object's
  initializer that calls a function, assigns or increments (the test is
  now a negative case).  All five pass under gcc.
- [x] **CC-37** A runtime test shall not depend on unspecified
  behaviour.  `native_c99_types.c`, `native_c99_inline_semantics.c` and
  `native_gnu89_inline.c` call a function with only an inline
  definition; whether that uses the inline or an external definition is
  unspecified (C11 6.7.4p7), and gcc's build does not link.  The tests
  now call only functions with an external definition (or a static
  inline one) and check the linkage rules in the assembly: C99 inline
  alone, `extern inline`, and inline beside an `extern` declaration.
  The gnu89 test asserted that `extern inline` is emitted externally,
  the reverse of GNU89 -- and cc did so; it now emits it as a local copy
  with no external symbol, as gcc emits none.  All three link and run
  under gcc.
- [x] **CC-38** `native_gnu_asm_goto.c` shall number asm-goto labels as
  GNU C does: after outputs and inputs, so with one `+r` output the
  first label is `%l2`, not `%l0`.  gcc rejects the test; cc fails it
  too (CC-27).  cc numbered labels after the outputs and inputs but
  counted a `+` output once; it now counts it twice, as an output and
  its implicit input, in both the check (sema) and the rendering.  A
  negative case checks that `%l0` is rejected.
- [x] **CC-39** The default `gnu17` mode shall treat trigraphs as gcc's
  GNU modes do, or the trigraph tests shall name an ISO mode:
  `native_c95_trigraph.c` and `native_c99_phase12_trigraph_splice.c`
  rely on trigraphs in `gnu17`, where gcc ignores them.  Both: the
  preprocessor replaces trigraphs only in the ISO modes before C23, or
  with `-trigraphs`, as gcc does; the parser's second trigraph pass over
  already-preprocessed text is gone, and so is the `-trigraphs` the
  gcc bootstrap frontend forced; the two tests name `-std=c99`, and
  `run_pp_postc99.sh` checks gnu17 with and without `-trigraphs`.
- [x] **CC-40** A `clang_*` test shall pass under clang.  Of five, two
  do (`asm_goto_quirks`, `builtin_aliases`).
  `native_clang_blocks_decl.c` assigns a function to a block pointer,
  which clang rejects -- cc treats blocks as function pointers;
  `native_clang_ext_vector_attr.c` initializes a vector from, and casts
  one to, a scalar, which clang (and gcc) reject;
  `native_clang_attr_placement.c` uses `__attribute__((unused));` as a
  statement, which clang rejects and gcc accepts -- it tests gcc
  behaviour under a clang name.  Now: the blocks test declares, copies
  and stores block pointers (valid with `clang -fblocks`); the vector
  test uses `ext_vector_type` where `__has_attribute` says it exists and
  a fallback where not; the placement test is `native_gnu_attr_placement.c`.
  All four `clang_*` tests pass under clang and cc.  The vector test
  showed cc had no vector types at all (CC-71): `vector_size(16)` gave
  a 4-byte scalar.  cc now rejects both vector attributes ("vector types
  are not supported") and `__has_attribute` says 0 for them.
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
- [x] **CC-46** A check of generated assembly shall show the property
  under test.  30 of the 190 `grep` checks only look for `.code32` in
  `-m32 -S` output, which shows the flag was read, not that the code is
  right: the -m32 output of those tests is never assembled or run.
  Others look for `jne`, `movl` or `jmp`, which almost any function
  contains.  *Traced.*  All 45 such checks are replaced: 44 programs are
  now built at -m32 and run, and `native_arith.c` (no `main`) looks for
  the `imull` its multiply needs.  Running them found two -m32
  miscompiles the greps had hidden, both fixed: `return 7;` from a long
  long function left the high word unset, and every signed 64-bit (and
  every int) overflow builtin gave the wrong answer.
- [x] **CC-47** The suite shall use a private temporary directory.  It
  writes 310 fixed `/tmp/cc_*` paths, so two runs (two CI jobs on one
  host, or two users) overwrite each other's files.  *Traced.*  The
  Makefile makes one with `mktemp -d` (`CC_TMP`, which a caller may
  set), exports it to the scripts and removes it when the run passes; a
  failed run leaves it to be looked at.  Every `/tmp/cc_*` path in the
  Makefile and the 14 scripts that wrote them is now under `CC_TMP`, and
  each script makes and removes its own when run alone.  No fixed
  `/tmp` path is left.

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

101 requirements: 93 met, 8 open.

The structural refactor and the target-neutral IR are planned in
`usr.bin/cc/docs/refactor-plan.md` and `usr.bin/cc/docs/neutral-ir-plan.md`.
