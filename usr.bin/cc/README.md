# usr.bin/cc

Substrate's own C compiler.  It preprocesses, compiles and assembles C
for x86-64 and i386 (System V psABI), and drives an assembler and a
linker to produce objects, executables and shared objects.

## What it does

```
.c -> preprocessor -> lexer -> parser -> sema -> SSA -> passes -> GAS assembly -> as -> ld
      frontend/preproc.c      frontend/         middle/          backend/x86/
```

- **Preprocessor** in-tree (`frontend/preproc.c`); `cc -E` and `cpp`
  (a link to `cc`) run it.  No external `cpp` is used.
- **Languages:** `-std=c90`, `c95`, `c99`, `c11`, `c17`, `c23` and the
  GNU variants; the default is `gnu17`.  GNU extensions (statement
  expressions, `__attribute__` (`cleanup(fn)` runs fn on every way out
  of the variable's scope), inline asm with goto, file-scope
  `asm("...")` (written to the `.s` as is, before the functions),
  `__builtin_*`, computed goto, ...) are supported.
- **Targets:** `-m64` (x86-64) and `-m32` (i386; non-PIE unless
  `-fPIC`).  A host build defaults to x86-64; on substrate, cc defaults
  to the architecture it was built for.  Data layout and calling
  conventions follow the psABI and are checked against gcc.
- **Optimization:** `-O1` and up fold constants and remove dead
  values.  Every value lives in a stack slot, cached in a few registers;
  the code is correct, not fast.
- **Assembler and linker:** `$AS` / `$LD` if set, else `../as/as` and
  `../ld/ld` relative to the `cc` binary (the tree's layout), else `as`
  and `ld` beside it (an installed layout), else the ones on `PATH`.

The full option list is the man page, `usr.man/man1/cc.1`.

## How it is checked

`make -C tests/usr.bin/cc` runs the suite.  The parts that matter most:

- `run_codegen_diff.sh`: every program in `tests/usr.bin/cc/codegen/`
  is built by gcc and by cc at -m64/-m32 and -O0/-O2; output and exit
  status must match.
- `run_codegen_random.sh`: 40 generated arithmetic programs, the same
  way.
- `run_codegen_abi.sh`: objects from cc and gcc call each other.
- `run_selfhost.sh`: a cc built by cc passes all of the above, and a cc
  built by *that* writes byte-identical assembly for cc's own sources.
- `run_negative.sh`: invalid programs are rejected with the right
  diagnostic (`negative/CASES.txt`).

gcc (or clang, for the clang-compatibility tests) is the judge of what
is correct.  `docs/cc-audit.md` at the top of the tree lists what the
audit found and how each item was met, with what each still does not
do.

## Build and install

```sh
make -C usr.bin/cc NATIVE_BUILD=1   # a cc that runs on the host
make -C usr.bin/cc                  # a cc that runs on substrate (i386)
make -C usr.bin/cc ARCH=x86_64      # ... on 64-bit substrate (obj-x86_64/cc)
```

On the host, the in-tree toolchain is installed under `/opt/substrate`:
`bin/cc`, `bin/cpp`, `bin/as`, `bin/ld`, and cc's own headers in
`lib/substratecc/include/`.

## Layout

| Directory | |
|---|---|
| `cmd/` | the driver (`cc.c`) and the compile pipeline (`pipeline.c`) |
| `common/` | target data layout (`target.c`), language modes (`lang.c`), compilation settings (`options.c`), diagnostics (`diag.c`), inline-asm constraints (`asm_constraint.c`), strings and vectors (`util.c`) |
| `frontend/` | preprocessor, lexer, parser, sema, scalar type layout (`types.c`), integer constant expressions (`consteval.c`) |
| `middle/` | AST to SSA (`ast2ir.c`), legalization, optimization passes, SSA utilities |
| `backend/` | the assembly emitter for both targets |
| `ir/` | a textual form of SSA with a verifier, normalizer and differ (`ir-verifier`, `ir-normalize`, `ir-diff`); cc does not write it yet |
| `include/` | the compiler's internal headers |
| `resource/include/` | headers cc supplies to programs (`x86intrin.h`, ...) |
| `docs/` | `refactor-plan.md`, `neutral-ir-plan.md` |

`ARCHITECTURE.md` describes the design; `SPEC.md` the requirements.

## Two options to know

- `--bootstrap-gcc` compiles C with the host's gcc instead, then
  assembles and links as usual.  It predates the compiler handling
  ordinary C and is kept for comparison.
- `-emit-ssa FILE.ir` runs `ir-verifier` on a textual IR file.
- `-fdump-ir` writes cc's own IR, as the back end gets it, to `NAME.ir`
  in that text form; `-fdump-ir=all` writes it after each middle-end
  pass (`NAME.lower.ir`, `NAME.legalize.ir`, `NAME.opt.ir`).
