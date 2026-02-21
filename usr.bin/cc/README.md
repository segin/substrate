# usr.bin/cc

`usr.bin/cc` is the in-tree C compiler work area.

## Status

Phase-4 (code quality + parity slice) is implemented:

- `cc` driver skeleton (`cmd/cc.c`) with Unix-style stage control.
- preprocessing stage (`-E`) via system `cpp`.
- assembly and link stages via system `as` and `ld`.
- native C subset pipeline:
  - lexer/parser/sema for a strict C subset
  - AST to SSA-like lowering
  - SSA middle-end optimization passes (`-O1+`): constant folding + dead temp elimination
  - GAS emitter for x86-64 and x86-32
  - backend stack-slot compaction (linear-scan style slot reuse) to reduce frame size
- existing SSA utilities remain available:
  - `ir-verifier`
  - `ir-normalize`
  - `ir-diff`

Native C support is intentionally limited for now:
- scalar types: `int`, `double`, `void`
- statement subset: local declarations, assignments, expression statements, `return`
- expressions: numeric literals, identifiers, `+ - * /`, parentheses, function calls, assignment expressions
- function declarations/definitions with fixed params or `...` variadics
- SysV AMD64 ABI lowering for mixed integer/SSE arguments including stack overflow arguments
- i386 ABI lowering with cdecl stack args/params, including `double` arithmetic/casts/call/return handling
- debug assembly directives with `-g` (`.file`, `.loc`, `.cfi_*`)
- no control-flow statements (`if`, `for`, `while`, `switch`) yet
- no pointers/structs/arrays yet

For non-supported C sources, use `--bootstrap-gcc` as a temporary fallback.

## Supported driver options (phase-0)

- `-E`, `-S`, `-c`, `-o`
- `-std=c99`
- `-O0`..`-O3`
- `-m32`, `-m64`
- `-g`
- `-Wall`, `-Werror`
- `-fPIC`, `-shared`, `-pthread`
- `-I`, `-D`, `-U`
- `-v`, `-###`
- `-emit-ssa` (currently verifies a `.ir` file via `ir-verifier`)
- `--bootstrap-gcc` fallback for full C front-end while native subset expands

## Build

```sh
make -C usr.bin/cc NATIVE_BUILD=1
```

## Examples

Preprocess only:

```sh
usr.bin/cc/cc -std=c99 -E foo.c
```

Assemble and link assembly input:

```sh
usr.bin/cc/cc -c hello.s -o hello.o
usr.bin/cc/cc hello.o -o hello
```

Native subset C path:

```sh
usr.bin/cc/cc -S native_arith.c -o native_arith.s
usr.bin/cc/cc native_main.c -o native_main
usr.bin/cc/cc native_phase1.c -o native_phase1
```

Temporary bootstrap C path (for wider C support):

```sh
usr.bin/cc/cc --bootstrap-gcc -std=c99 -c foo.c -o foo.o
```

Build `bin/sh` through the new driver (temporary bootstrap mode):

```sh
make -C usr.bin/cc NATIVE_BUILD=1
make -C bin/sh clean NATIVE_BUILD=1
make -C bin/sh NATIVE_BUILD=1 CC='/home/segin/test/usr.bin/cc/cc --bootstrap-gcc'
```
