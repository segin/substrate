# cc: a target-neutral IR with per-target lowering

Status: step 0 done (`cc -fdump-ir`, the opcode table, golden IR tests); step 1 in progress.  This document says what the compiler does
today, the shape we want, and an order of work in which every step
lands on its own with the test gate green.

## 1. Where cc is today

The pipeline (`cmd/pipeline.c`) is:

```
preproc -> lexer -> parser -> sema -> ast2ir -> legalize -> opt -> emit_s
                                        (SSA)                 (x86-64 | i386 text)
```

The IR between `ast2ir` and `emit_s` (`include/cc_ssa.h`) is already
*mostly* neutral, which is why this is a plan and not a rewrite:
opcodes are generic (`ADD`, `LOAD`, `CMP`, `CALL`, `BR_COND`, ...), and
`legalize.c` (validation, dead code after terminators) and
`passes/opt.c` (folding, dead-code removal) know nothing of x86.  What
keeps it from being neutral:

1. **Values have two types, not widths.**  A value is `CC_VAL_I64` or
   `CC_VAL_F64`; its real width lives in a side table, `value_sizes`,
   which defaults to the *pointer size*.  There is no truncate or
   extend: lowering writes `shl`/`shr` pairs and relabels sizes.  Most
   of the audit's integer miscompiles (CC-1 to CC-8 in
   `docs/cc-audit.md`) were a value whose recorded width was wrong
   for the operation reading it.
2. **The target is decided inside lowering.**  `ast2ir.c` reads
   `g_pointer_size_bytes` 76 times.  It classifies call arguments and
   returns for the System V ABIs itself (`CC_CALL_ARG_ABI_AGG_MEM`,
   `AGG_REG`, `LDOUBLE`, the 16-byte and 4-byte aggregate limits), picks
   the i386 struct-return convention, and writes inline-asm text naming
   `%rsp`/`%esp`.  The IR that leaves `ast2ir` is already an i386 or an
   x86-64 program.
3. **One emitter file holds both back ends.**  `backend/emit_s.c` is
   6.5k lines: shared slot/spill helpers, `emit_x86_64` (from line
   4226) and `emit_i386` (from 5981).  i386 carries 64-bit integers as
   stack-slot word pairs and does all of that inside the emitter, so
   every 64-bit operation has an i386 special case at the point of
   emission.
4. **A dead MIR.**  `backend/select.c` lowers SSA to `cc_mir_*`, it is
   validated, then freed; nothing emits from it.
5. **The target is an enum.**  `cc_target_t` is `X86_64 | I386`;
   type sizes and alignments are spread across the front end, lowering
   and emitter as `pointer_size == 4 ? ... : ...`.

## 2. The shape we want

```
AST --ast2ir--> neutral IR --verify--> opt (neutral)
                      |
                      v
         ABI lowering   (per target: x86-64 SysV, i386 SysV)
                      |
         legalization   (per target: split/expand what the target lacks)
                      |
         selection + regalloc + emit   (per target, thin)
```

### 2.1 Typed values

Every value gets a real type: `i1 i8 i16 i32 i64 ptr f32 f64 f80`.
`ptr` stays abstract until legalization.  Width changes are explicit
operations: `zext sext trunc fpext fptrunc sitofp uitofp fptosi
fptoui ptrtoint inttoptr`.  The `value_sizes` side table and the
`shl`/`shr` idiom go away; `opt.c`'s width-aware folding
(`fold_int_binop`, `value_is_word32`) becomes plain type-directed
folding.  The verifier rejects an operation whose operand types do not
match.

### 2.2 Calls carry the C signature, not an ABI

A call in neutral IR says what C says: parameter and return types,
with aggregates as `agg(size, align)` passed by value, plus
`variadic` and the fixed-argument count.  It does not say "in
memory" or "in registers".  Struct returns are a typed `agg` result.
Aggregate copies are an explicit `memcpy(dst, src, size, align)`
operation, not open-coded loads and stores.

### 2.3 A target descriptor

`cc_target_t` becomes `const cc_target_desc_t *`: pointer size, size
and alignment of every C type (`long`, `long double`, `_Bool`, ...),
the legal integer widths, the stack alignment, and hooks for the ABI
pass, the legalizer and the emitter.  The front end asks it for type
sizes (sizeof, layout, `__SIZEOF_*__`) so that the two answers can't
drift.  Adding a target means adding a descriptor and its three hooks.

### 2.4 ABI lowering (per target)

Turns neutral calls, parameters and returns into explicit ones: the
x86-64 classification (INTEGER / SSE / MEMORY, eightbytes, `%al` for
variadic calls, `long double` in memory) and the i386 rules (all on
the stack, 4-byte slots, hidden return pointer popped by the callee
with `ret $4`).  What `ast2ir` does today at lines ~6766, ~10941 and
~14637, and `emit_i386_call`'s argument pushing, move here.  Output is
still neutral IR, with the hidden pointer and the memory copies
written out.

### 2.5 Legalization (per target)

Rewrites what the target cannot do in one instruction:

- **i386:** split `i64` into `i32` pairs (add/adc, sub/sbb, the
  three-multiply product, compares on both words), turn 64-bit
  division and modulo into `__divdi3` / `__udivdi3` / `__moddi3` /
  `__umoddi3` calls, and resolve `ptr` to `i32`.
- **x86-64:** resolve `ptr` to `i64`; 128-bit integers, if they are
  ever supported (CC-22), split here.
- **both:** `f80` (x87) and `f32` rounding (today's `FROUND32`).

After legalization every value is a width the target has a register
for, and the emitters lose their 64-bit-pair paths.

### 2.6 Thin emitters

`backend/x86/` with `common.c` (frame layout, spill choice, slot
addressing, the GAS directives), `x86_64.c` and `i386.c`.  Each
selects instructions for legalized IR only.  The MIR either becomes
the selection output (if it earns its keep) or is deleted.

Inline asm stays opaque target text.  The ABI pass and the emitter
bind its operands; the verifier only checks the constraint letters
against the descriptor.

## 3. Order of work

Each step is a series of commits that leaves every check in §4 green.
None of them changes the code `cc` writes, except where a step fixes
a bug, and that bug gets a regression test of its own.

| Step | Work | Size |
|---|---|---|
| 0 | **Make the IR visible.**  The text form is the existing one the `ir-verifier`, `ir-normalize` and `ir-diff` tools read (typed, LLVM-like; see `tests/usr.bin/cc/valid.ir`), so those tools become the IR's verifier and the golden tests' normalizer and differ.  `cc -fdump-ir` writes it (after each pass with `-fdump-ir=all`); the verifier gains an opcode and type table and checks operand types, which it does not today; golden-IR tests under `tests/usr.bin/cc/ir/` compare normalized dumps.  Today's IR has no phi, so the dumps are the phi-free subset until locals are promoted.  Decide the MIR's fate: delete `select.c` unless step 5 needs it.  **Done.**  `middle/ssa_dump.c` writes the form; a value written more than once is a variable (`undef` at the entry, written by `set`), and an aggregate value is the address of its memory.  The opcode table checks classes (integer, floating, aggregate), not widths, until step 1.  `run_ir_golden.sh` compares `tests/usr.bin/cc/ir/*.c` with their `.m64.ir`/`.m32.ir` goldens and verifies every `codegen/` dump after each pass.  The MIR was deleted in refactor step 1. | small |
| 1 | **Typed values.**  Add the types and the conversion opcodes.  `ast2ir` writes them; the emitters accept them; the verifier checks them.  Remove `value_sizes` and the `shl`/`shr` conversions last.  **In progress.**  Integer conversions are one `EXT` (`sext`/`zext` of the low N bits) that legalization expands into the shifts and masks the emitters run.  The dump says which IR is neutral and which legal (`stage`); the verifier holds neutral IR to an integer model: an integer narrower than the word is held extended to the word, so it may be an operand of any integer operation up to the word (C's promotions), while narrowing and widening past the word are explicit.  Bringing lowering into line found three i386 miscompiles (CC-103 `__builtin_bswap64`, CC-104 `++`/`--` on `long long`, CC-106 `switch` on `long long`).  The three side tables are one, `value_ty` (`cc_ssa_type_t`: kind int/float/aggregate, size, extension), read and written through the `cc_ssa_value_*` accessors; `value_sizes` is gone. | large |
| 2 | **Target descriptor.**  Replace `cc_target_t`, `g_pointer_size_bytes` and the front end's own size tables with descriptor queries.  Mechanical, but it touches every file. | medium |
| 3 | **ABI pass.**  Calls carry C signatures; `ast2ir` stops classifying; `abi_x86_64.c` and `abi_i386.c` do it.  `emit_*_call` read the explicit result. | large |
| 4 | **i386 legalization.**  64-bit splitting and the division libcalls move from the emitter into `legalize_i386.c`; delete the emitter's word-pair paths. | large |
| 5 | **Split the emitter.**  `emit_s.c` into `backend/x86/{common,x86_64,i386}.c`, the code moved verbatim first (history kept with the git-cp trick), then reduced to selection over legalized IR. | medium |
| 6 | **Optional: a third target**, as proof the seams hold.  Nothing in substrate needs one now. | — |

Step 1 is the one that pays for itself first: it removes the class of
bug the audit found most.  Steps 2 to 4 are what make the IR neutral.
Step 5 is tidying once the emitters have less to do.

## 4. The gate for every step

- `python3 run_lines.py` over `tests/usr.bin/cc/Makefile` (or
  `make -C tests/usr.bin/cc`): no new failures.
- `run_codegen_diff.sh`: every program under `codegen/` matches gcc,
  in -m64 and -m32, at -O0 and -O2.
- `run_codegen_random.sh`: seeds 1-40 in the same four modes.
- `run_codegen_abi.sh`: `cc`- and gcc-compiled objects call each other
  both ways (the guard for steps 3 and 4).
- The golden-IR tests from step 0.
- On target: deploy `cc`, the `tests/rootfs64/boot-test.sh` boot, and
  the i386 hello build and run.

## 5. Not in this plan

- **Promoting locals to SSA registers (mem2reg) and phi nodes.**  The
  IR keeps locals in stack slots.  Typed values make mem2reg possible
  later; it is an optimization, not a neutrality issue.
- **A new register allocator.**  The emitters keep the current
  spill-by-next-use scheme until step 5 shows it is the bottleneck.
- **Open audit items** (CC-19 VLA sizeof, CC-34 function-pointer
  levels, CC-49 nested functions) are front-end work.  They are fixed
  on their own, before or after this, not as part of it.  CC-20 (wrong
  -m64 code from an i386-hosted `cc`) is a host-width bug in lowering
  and folding.  Step 1 should make it easy to find, but it is not
  scheduled here.

## 6. Risks

- **ABI regressions are silent.**  A wrong classification still links
  and runs until a struct of the wrong shape crosses a call.  The
  mixed-compiler ABI test must grow (more aggregate shapes, variadic
  calls, `long double`, sret) *before* step 3, not after.
- **The emitter's spill logic is subtle.**  Step 5 moves code
  verbatim before changing it, so that the diff that changes it is
  small.
- **Step 1 touches all of `ast2ir.c`** (14.9k lines).  It lands one
  operator family at a time (casts, then arithmetic, then compares,
  then loads and stores), with the side table kept until the last of
  them.
