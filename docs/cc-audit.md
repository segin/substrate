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
| 2. Programs on substrate | 2 | 2 |
| 3. Tests | 2 | 1 |
| 4. ABI and other code generation | 10 | 6 |
| 5. Driver and predefined macros | 2 | 0 |
| 6. The restored suite's failing commands | 10 | 0 |
| 7. Documentation | 1 | 0 |

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
- [ ] **CC-19** `sizeof` of a variable-length array shall be its run-time
  size.  It is rejected: "unsupported sizeof operand in lowering".
  *Reproduced.*
- [ ] **CC-20** Where cc runs on i386 and writes x86-64 code (`-m64`),
  the code shall be what a 64-bit-hosted cc writes.  24 of 200 random
  -m64 builds by a `gcc -m32` build of cc are wrong.  *Reproduced.*
- [x] **CC-35** Each arm of `?:` shall be converted to the result's
  type.  An arm narrower than an `unsigned long long` result was moved
  into it unconverted, so on i386 its high word was whatever the stack
  held: `c ? (short)0 : v0` gave `f7d0710000000000`.  Found when local
  initializers began to be converted (CC-15), which moved the garbage.
  *Reproduced* (-m32).  `codegen/ternary_widen.c`.
- [ ] **CC-34** `(*fp)(...)` shall call `fp` when `fp`'s function
  returns a pointer.  `int *(*pf)(...)` has the pointer depth of a
  pointer to a function pointer, and lowering keeps the dereference for
  those, so it loads from the function's code and calls that: SIGSEGV.
  `pf(...)` works.  The type system does not record which pointer
  levels belong to a function declarator.  *Reproduced* (all modes).

## 5. Driver and predefined macros

- [ ] **CC-21** If a tool the driver runs cannot be executed, then the
  driver shall say so.  When `as` exited 127 (not runnable), cc exited 1
  and printed nothing.  *Reproduced.*
- [ ] **CC-22** The compiler shall predefine `__SIZEOF_INT128__` only if
  `__int128` works.  It is predefined for -m64, and a program using
  `__int128` compiles and dies of SIGSEGV.  *Reproduced*; also suite
  commands 87, 823 and 824.

## 6. The restored suite's failing commands

Command numbers are those `run_lines` gives the `test` recipe of
`tests/usr.bin/cc/Makefile` at `ad09b201a`; all fail before this audit's
changes as well.  *Suite.*

- [ ] **CC-23** `native_c99_complex_decl` shall run successfully (98).
- [ ] **CC-24** `native_c11_alignas_zero.c` shall compile and run
  (200, 201).
- [ ] **CC-25** `native_c99_flexible_array_global_init` shall run
  successfully (321).
- [ ] **CC-26** `native_c99_consteval_limits.c` shall compile and run
  (350, 351).
- [ ] **CC-27** `native_gnu_asm_goto.c` shall compile, run, and emit its
  `jz .L…` targets for -m64 and -m32 (497–505).
- [ ] **CC-28** `run_postc99_c23.sh` shall pass (764).
- [ ] **CC-29** `run_simd_intrinsics.sh` shall pass (789).
- [ ] **CC-30** If an asm input's constraint names the register of an
  early-clobber output, then cc shall reject it
  (`native_bad_gnu_asm_early_clobber_fixed.c`, 832, 833).
- [ ] **CC-31** If a `noreturn` function has a non-void return type,
  then cc shall reject it (`native_bad_attr_noreturn_nonvoid.c`, 835).
- [ ] **CC-32** The suite's `__int128` expectations shall match the
  compiler's behaviour once CC-22 is settled (87, 823, 824).

## 7. Documentation

- [ ] **CC-33** `usr.bin/cc/README.md` shall describe the compiler as it
  is.  It describes "phase-9", a scalar-only subset, `-E` via the system
  `cpp`, and `--bootstrap-gcc` as the way to compile ordinary C.

---

35 requirements: 17 met, 18 open.
