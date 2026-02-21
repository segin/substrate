# usr.bin/cc

`usr.bin/cc` is the in-tree C compiler work area.

## Status

Phase-9 (expanded C99 expression/declaration/control slice) is implemented:

- `cc` driver skeleton (`cmd/cc.c`) with Unix-style stage control.
- preprocessing stage (`-E`) via system `cpp`.
- assembly and link stages via system `as` and `ld`.
- native C subset pipeline:
  - lexer/parser/sema for a strict C subset
  - function declarations/prototypes with signature compatibility checks across declarations/definitions
  - AST to SSA-like lowering
  - SSA middle-end optimization passes (`-O1+`): constant folding + dead temp elimination
  - GAS emitter for x86-64 and x86-32
  - backend stack-slot compaction (linear-scan style slot reuse) to reduce frame size
  - branch-capable lowering/emission for `if`/`else`, `while`, `do-while`, `for`, and `switch`/`case`/`default` control flow
  - loop flow statements: `break` and `continue`
  - C95 lexical forms: digraph braces (`<%`/`%>`) and trigraph normalization
  - C99 declaration-specifier combinations for current scalar subset (`_Bool`, `char`/`unsigned char`, `short`/`unsigned short`, `int`/`unsigned int`, `long long`/`unsigned long long`, `float`, qualifiers/storage-class keywords)
  - C99 `for`-init declarations with loop-local scope
  - one-level typed pointers (`T*`) in declarations, parameters, and return types
  - expression extensions: `%`, unary `+`/`!`/`~`, short-circuit `&&`/`||`, bitwise/shift ops (`& | ^ << >>`), comma operator, compound assignments (`+= -= *= /= %= &= |= ^= <<= >>=`), and prefix/postfix `++/--` with C expression-value semantics
  - expression extensions: ternary conditional (`?:`), scalar casts (`(int)`, `(double)`, etc.), and `sizeof` for supported scalar types
  - integer literal suffix typing for unsigned/long-long forms (`u`, `ul`, `ull`, etc.) in semantic/lowering paths
  - control-flow extensions: C labels + `goto`
- existing SSA utilities remain available:
  - `ir-verifier`
  - `ir-normalize`
  - `ir-diff`

Native C support is intentionally limited for now:
- scalar types: `_Bool`, `char`, `unsigned char`, `short`, `unsigned short`, `int`, `unsigned int`, `long long`, `unsigned long long`, `float`, `double`, `void`
- pointer types: one-level typed pointers to supported scalar/void types (e.g., `int *`, `double *`, `void *`)
- statement subset: local declarations, assignments, expression statements, `return`
- statement subset extension: `if (...) stmt [else stmt]` and block statements `{ ... }`
- statement subset extension: `while (...)`, `do ... while (...)`, `for (...; ...; ...)`, `switch/case/default`, labels/`goto`, `break;`, `continue;`
- expressions: numeric/character literals (decimal/octal/hex integers, simple character escapes), identifiers, `+ - * / %`, shifts (`<< >>`), bitwise (`& | ^ ~`), numeric comparisons (`== != < <= > >=`, including floating comparisons), logical operators (`! && ||`) with C truthiness for scalars (including floating `!= 0.0`), comma operator, ternary `?:`, scalar casts, `sizeof` (supported scalars), parentheses, function calls, assignment/compound-assignment expressions, prefix/postfix `++/--`
- pointer expressions: unary address-of (`&`) for identifier lvalues, unary dereference (`*`) for pointers, pointer/null equality comparisons, pointer argument/return lowering, indirect assignment through pointers (`*p = expr`), and pointer arithmetic (`ptr +/- int`, `int + ptr`) for non-`void*` pointers
- unsigned integer semantics are modeled for comparisons/division/modulo/right-shift in native lowering/codegen (`seta/setb`, `div`, logical right-shift)
- function declarations/prototypes and definitions with fixed params or `...` variadics
- SysV AMD64 ABI lowering for mixed integer/SSE arguments including stack overflow arguments
- i386 ABI lowering with cdecl stack args/params, including `double` arithmetic/casts/call/return handling
- debug assembly directives with `-g` (`.file`, `.loc`, `.cfi_*`)
- assignments inside conditional/loop blocks are supported via explicit SSA `mov` variable updates
- C95 lexing support includes digraph/trigraph forms for punctuation
- no structs/arrays yet (pointer-pointer arithmetic and compound assignments on indirect lvalues are still limited)

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
