# `usr.lib/demangle` Tasklist

Goal: implement a shared C demangling library (`libdemangle.a`) consumed by `c++filt`, `nm`, `objdump`, `addr2line`, and any other tool needing symbol demangling.

---

## 1. Public API (`include/demangle.h`)

- [x] `char *demangle(const char *mangled, int options)` — returns heap‑allocated demangled string, or `NULL` on failure. Caller frees.
- [x] `int demangle_buf(const char *mangled, char *buf, size_t bufsz, int options)` — demangle into caller‑provided buffer. Returns 0 on success, -1 on failure, -2 on truncation.
- [x] `void demangle_free(char *str)` — free a string returned by `demangle()` (for ABI safety if library uses custom allocator).
- [x] Options flags:
  - [x] `DEMANGLE_NO_PARAMS` — omit function parameter types.
  - [x] `DEMANGLE_NO_VERBOSE` — suppress `const`/`volatile`/`restrict`/`noexcept` qualifiers.
  - [x] `DEMANGLE_TYPES` — attempt to demangle bare type encodings (not just names).
  - [x] `DEMANGLE_AUTO` — auto‑detect mangling scheme (default).
  - [x] `DEMANGLE_ITANIUM` — force Itanium ABI.
  - [x] `DEMANGLE_RUST` — force Rust `_R` scheme.
  - [x] `DEMANGLE_DLANG` — force D `_D` scheme.
- [x] `const char *demangle_version(void)` — library version string.

## 2. Itanium ABI Demangler (primary)

### 2a. Parser Architecture
- [x] Recursive‑descent parser operating on `const char *` input with cursor.
- [x] No heap allocation during parse — build output directly into growable buffer.
- [x] Substitution table: fixed‑capacity array (256 entries), overflow = fail gracefully.
- [x] Template argument stack for nested template resolution.
- [x] Recursion depth counter with configurable limit (default 256).

### 2b. Core Grammar Coverage
- [x] `<mangled-name>` → `_Z <encoding>`.
- [x] `<encoding>` → `<name> <bare-function-type>` | `<special-name>`.
- [x] `<name>` → `<nested-name>` | `<unscoped-name>` | `<local-name>`.
- [x] `<nested-name>` → `N [<CV-quals>] [<ref-qual>] <prefix>+ <unqualified-name> E`.
- [x] `<unqualified-name>` → `<source-name>` | `<operator-name>` | `<ctor-dtor-name>` | `<unnamed-type-name>`.
- [x] `<source-name>` → `<number> <identifier>`.
- [x] `<operator-name>` → all standard C++ operators (`nw`, `na`, `dl`, `da`, `ps`, `ng`, `ad`, `de`, `co`, `pl`, `mi`, `ml`, `dv`, `rm`, `an`, `or`, `eo`, `ls`, `rs`, `eq`, `ne`, `lt`, `gt`, `le`, `ge`, `ss`, `nt`, `pp`, `mm`, `cm`, `pm`, `pt`, `cl`, `ix`, `qu`, `cv`, `li`, `v0`–`v9`).
- [x] `<ctor-dtor-name>` → `C1`/`C2`/`C3`/`CI1`/`CI2`, `D0`/`D1`/`D2`.
- [x] `<template-args>` → `I <template-arg>+ E`.
- [x] `<template-arg>` → `<type>` | `<expr-primary>` | `X <expression> E` | `J <template-arg>* E` (pack).

### 2c. Type Demangling
- [x] Builtin types: `v`, `w`, `b`, `c`, `a`, `h`, `s`, `t`, `i`, `j`, `l`, `m`, `x`, `y`, `n`, `o`, `f`, `d`, `e`, `g`, `z`, `Dd`, `De`, `Df`, `Dh`, `Di`, `Ds`, `Da`, `Dc`, `Dn`.
- [x] `<pointer-type>` (`P`), `<reference-type>` (`R`), `<rvalue-reference>` (`O`).
- [x] `<complex-type>` (`C`), `<imaginary-type>` (`G`).
- [x] CV qualifiers (`K`, `V`, `r`) applied to types.
- [x] `<function-type>` → `F [Y] <type>+ E` (with optional extern "C" marker `Y`).
- [x] `<array-type>` → `A <number> _ <type>`.
- [x] `<pointer-to-member>` → `M <type> <type>`.
- [x] `<template-param>` → `T_` | `T <seq-id> _`.
- [x] `<decltype>` → `Dt` / `DT`.
- [x] Vendor‑extended type → `u <source-name>`.

### 2d. Substitutions
- [x] Build substitution table during parse.
- [x] `S_`, `S <seq-id> _` lookups.
- [x] Standard substitutions: `St` (std), `Sa` (std::allocator), `Sb` (std::basic_string), `Ss` (std::string), `Si` (std::istream), `So` (std::ostream), `Sd` (std::iostream).

### 2e. Special Names
- [x] `TV <type>` — vtable.
- [x] `TT <type>` — VTT.
- [x] `TI <type>` — typeinfo.
- [x] `TS <type>` — typeinfo name.
- [x] `GV <name>` — guard variable.
- [x] `Tc`/`Th`/`Tv` — thunks (with offset parsing).
- [x] `T` — transaction clones (deferred).

### 2f. Expressions and Literals
- [x] Integer literals: `L <type> <number> E`.
- [x] Nullptr: `LDnE`.
- [x] Expression operators: `cl`, `ix`, `qu`, `st`, `sz`, `at`, `az`, etc.
- [x] Cast expressions: `cv <type> <expr>`.
- [x] Fold expressions (C++17): `fL`, `fR`, `fl`, `fr`.
- [x] Pack expansion: `sp`.

### 2g. Lambda / Closure Types
- [x] `Ul <type>* E [<number>] _`.

## 3. Rust Demangler (secondary)

### 3a. Detection and Legacy
- [x] Detect Rust v0 mangling: symbol starts with `_R`.
- [x] Detect Rust legacy mangling: symbol starts with `_ZN` and ends with `17h<hex_hash>E` pattern.
- [x] `DEMANGLE_AUTO`: try Rust v0 first if `_R`, then fall back to Itanium.
- [x] `DEMANGLE_RUST`: force Rust v0 only, return `NULL` for non‑Rust input.

### 3b. Parser Architecture
- [x] Recursive‑descent parser on `const char *` with cursor (same pattern as Itanium).
- [x] Backref table: dynamic array of `(start, end)` byte ranges into the mangled string.
- [x] Recursion depth counter (default limit 128).
- [x] Output via shared growable buffer (§5).

### 3c. Rust v0 Grammar — Paths (`<path>`)
- [x] `C <disambiguator>? <identifier>` — crate root.
- [x] `N <namespace> <path> <disambiguator>? <identifier>` — nested path.
  - [x] Namespace tag: `v` (value ns), `t` (type ns), lowercase = internal, uppercase = user‑visible.
- [x] `M <impl-path> <type>` — inherent impl `<Type>`.
- [x] `X <impl-path> <type> <path>` — trait impl `<Type as Trait>`.
- [x] `Y <type> <path>` — trait definition reference `<Type as Trait>`.
- [x] `I <path> <generic-arg>+ E` — generic instantiation `path::<A, B, ...>`.

### 3d. Rust v0 Grammar — Identifiers
- [x] `<identifier>` → `[u] <decimal-number> [_] <bytes>`.
  - [x] `u` prefix indicates Punycode‑encoded Unicode identifier.
  - [x] `_` separator present only when `<bytes>` starts with a digit or `_`.
- [x] Punycode decoding: implement RFC 3492 decoder for `u`‑prefixed identifiers.
- [x] Map decoded identifiers to `{ident}` display form for non‑ASCII names.

### 3e. Rust v0 Grammar — Types (`<type>`)
- [x] Basic types: single‑char encodings:
  - [x] `b` (bool), `c` (char), `e` (str), `u` (unit `()`), `a` (i8), `s` (i16), `l` (i32), `x` (i64), `n` (i128), `i` (isize).
  - [x] `h` (u8), `t` (u16), `m` (u32), `y` (u64), `o` (u128), `j` (usize).
  - [x] `f` (f32), `d` (f64), `z` (! / never), `p` (placeholder `_`), `v` (variadic `...`).
- [x] `R <lifetime>? <type>` — shared reference `&T` / `&'a T`.
- [x] `Q <lifetime>? <type>` — mutable reference `&mut T`.
- [x] `P <type>` — raw const pointer `*const T`.
- [x] `O <type>` — raw mut pointer `*mut T`.
- [x] `A <type> <const>` — array `[T; N]`.
- [x] `S <type>` — slice `[T]`.
- [x] `T <type>* E` — tuple `(A, B, ...)`.
- [x] `F <fn-sig>` — function pointer.
  - [x] `<fn-sig>` → `<binder>? U? (K <abi>)? <type>* E <type>`.
  - [x] `U` = unsafe, `K` = ABI (e.g. `KC` = `extern "C"`).
  - [x] `<binder>` → `G <base-62-number>` (higher‑ranked lifetimes).
- [x] `D <dyn-bounds> <lifetime>` — trait object `dyn Trait + 'a`.
  - [x] `<dyn-bounds>` → `<binder>? <dyn-trait>* E`.
  - [x] `<dyn-trait>` → `<path> <dyn-trait-assoc-binding>*`.
- [x] `B <backref>` — backreference to a previously parsed type.

### 3f. Rust v0 Grammar — Const Values (`<const>`)
- [x] `<const>` → `<type> <const-data>` | `B <backref>` | `p` (placeholder `_`).
- [x] Integer const: `<hex-digits> _` (value in hex, `n` prefix for negative).
- [x] Bool const: `0_` (false), `1_` (true).
- [x] Char const: Unicode scalar value as hex.

### 3g. Rust v0 Grammar — Lifetimes and Binders
- [x] `<lifetime>` → `L <base-62-number>` (de Bruijn index).
- [x] `<base-62-number>` → `_` (0) | `<digit>+ _` (value + 1, base 62: `0-9 a-z A-Z`).
- [x] Lifetime display: `'_` for erased, `'a`, `'b`, ... for bound lifetimes.

### 3h. Rust v0 Grammar — Disambiguators
- [x] `<disambiguator>` → `s <base-62-number>`.
- [x] Display: suppress in output (internal compiler detail), unless verbose mode requested.

### 3i. Rust v0 Grammar — Backreferences
- [x] `B <base-62-number>` — refers to a byte offset in the mangled string.
- [x] Re‑parse from that offset to recover the referenced entity.
- [x] Guard against circular backrefs (depth limit).

### 3j. Rust Legacy Demangling
- [x] Pattern: `_ZN <length1> <ident1> ... <length_n> <hash_17chars> E`.
- [x] Strip trailing `::h<hex_hash>` from output.
- [x] Convert `$` escapes: `$LT$` → `<`, `$GT$` → `>`, `$RF$` → `&`, `$LP$` → `(`, `$RP$` → `)`, `$C$` → `,`, `$SP$` → `@`, `$BP$` → `*`, `$u20$` → ` `, `$u27$` → `'`, `$u5b$` → `[`, `$u5d$` → `]`, `$u7b$` → `{`, `$u7d$` → `}`, `$u3b$` → `;`, `$u7e$` → `~`, etc.
- [x] Join components with `::`.
- [x] Fallback: if parsing fails, return `NULL`.

### 3k. Output Formatting
- [x] Paths separated by `::`.
- [x] Generic args in `<A, B>` angle brackets.
- [x] Function pointers: `fn(A, B) -> C` or `unsafe extern "C" fn(A) -> B`.
- [x] References: `&T`, `&mut T`, `&'a T`.
- [x] Tuples: `(A, B, C)`.
- [x] Arrays: `[T; N]`.
- [x] Trait objects: `dyn Trait<Assoc = T> + 'a`.
- [x] Closures: `crate::module::{closure#0}`.
- [x] Shims: `crate::module::{shim:vtable#0}`.

## 4. D Language Demangler (tertiary)

### 4a. Detection
- [x] Detect D mangling: symbol starts with `_D`.
- [x] `DEMANGLE_AUTO`: try D if `_D` prefix and Itanium parse fails.
- [x] `DEMANGLE_DLANG`: force D only.

### 4b. Parser Architecture
- [x] Recursive‑descent parser with cursor.
- [x] LName‑based: `<number> <chars>` length‑prefixed identifiers.
- [x] Recursion depth guard (default limit 128).
- [x] Output via shared growable buffer (§5).

### 4c. D Mangling Grammar — Qualified Names
- [x] `<MangledName>` → `_D <QualifiedName> <Type>`.
- [x] `<QualifiedName>` → `<SymbolName>+`.
- [x] `<SymbolName>` → `<LName>` | `<TemplateInstanceName>`.
- [x] `<LName>` → `<Number> <chars>` (length‑prefixed UTF‑8 identifier).
- [x] `<Number>` → decimal digit sequence.

### 4d. D Mangling Grammar — Template Instances
- [x] `<TemplateInstanceName>` → `__T <LName> <TemplateArgs> Z`.
- [x] `<TemplateArgs>` → `<TemplateArg>+`.
- [x] `<TemplateArg>` :
  - [x] `T <Type>` — type argument.
  - [x] `V <Type> <Value>` — value argument.
  - [x] `S <QualifiedName>` — symbol argument (alias).
  - [x] `X <Number> <ExternMangledName>` — external (C/C++) symbol.
- [x] `<Value>` — encoded integer/float/string/null values.

### 4e. D Mangling Grammar — Types
- [x] Basic types: `v` (void), `g` (byte), `h` (ubyte), `s` (short), `t` (ushort), `i` (int), `k` (uint), `l` (long), `m` (ulong), `f` (float), `d` (double), `e` (real), `o` (ifloat), `p` (idouble), `j` (ireal), `q` (cfloat), `r` (cdouble), `c` (creal), `b` (bool), `a` (char), `u` (wchar), `w` (dchar).
- [x] `A <Type>` — dynamic array `T[]`.
- [x] `G <Number> <Type>` — static array `T[N]`.
- [x] `H <Type> <Type>` — associative array `V[K]`.
- [x] `P <Type>` — pointer `T*`.
- [x] `E <QualifiedName>` — enum type.
- [x] `C <QualifiedName>` — class type.
- [x] `S <QualifiedName>` — struct type.
- [x] `I <QualifiedName>` — interface type.
- [x] `D <Type>` — delegate.
- [x] `F ... Z <Type>` — function type (with parameter encoding).
  - [x] Parameter storage classes: `J` (out), `K` (ref), `L` (lazy), `M` (scope), `N` (return).
  - [x] Calling conventions: `F` (D), `U` (C), `W` (Windows), `V` (Pascal), `R` (C++).
- [x] `B <Number> <Type>` — tuple type.
- [x] `n` — typeof(null).

### 4f. D Mangling Grammar — Qualifiers
- [ ] `x` — const.
- [ ] `y` — immutable.
- [ ] `O` — shared.
- [ ] `Ng` — inout (wild).
- [ ] Combine qualifiers: `xO` = shared const.

### 4g. D Mangling Grammar — Function Attributes
- [ ] `Na` — pure.
- [ ] `Nb` — nothrow.
- [ ] `Nc` — ref.
- [ ] `Nd` — @property.
- [ ] `Ne` — @trusted.
- [ ] `Nf` — @safe.
- [ ] `Ni` — @nogc.
- [ ] `Nj` — return ref.

### 4h. D Mangling — Special Sequences
- [ ] `__lambda<N>` — lambda/anonymous function.
- [ ] `__dgliteral<N>` — delegate literal.
- [ ] `__unittest<N>` — unit test.
- [ ] `__modctor` — module constructor.
- [ ] `__moddtor` — module destructor.
- [ ] `__aggr<N>` — aggregate.
- [ ] `__initZ` — init symbol.
- [ ] `__ClassZ` — class info.
- [ ] `__vtblZ` — vtable.
- [ ] `__InterfaceZ` — interface info.

### 4i. Output Formatting
- [ ] Module‑qualified names separated by `.`: `std.stdio.writeln`.
- [ ] Template instances: `Foo!(int, string)`.
- [ ] Function signatures: `int function(int, ref int) pure nothrow @safe`.
- [ ] Arrays: `int[]`, `int[5]`, `int[string]`.
- [ ] Pointers: `int*`.
- [ ] Delegates: `int delegate(int)`.
- [ ] Qualifiers: `const(int)`, `immutable(char[])`, `shared(int*)`.

### 4j. Edge Cases
- [ ] Symbols with back‑references within qualified names.
- [ ] Deeply nested template instantiations.
- [ ] Mixed D + C linkage symbols (`__T ... X ...`).
- [ ] Symbols from D runtime (`_d_*` symbols — these use C mangling, pass through).

## 5. Growable Output Buffer

- [ ] Internal `struct demangle_buf { char *data; size_t len, cap; }`.
- [ ] `buf_append(buf, str, len)` — grow by 2× when full.
- [ ] `buf_appendc(buf, ch)` — single char.
- [ ] `buf_printf(buf, fmt, ...)` — formatted append.
- [ ] Initial capacity: 256 bytes (covers most symbols without realloc).
- [ ] Final NUL termination.

## 6. Error / Failure Handling

- [ ] Parse failure at any point: return `NULL` from `demangle()`.
- [ ] Never crash on malformed input — every code path must handle unexpected bytes.
- [ ] Recursion depth exceeded: return `NULL`.
- [ ] Substitution table overflow: return `NULL`.
- [ ] Memory allocation failure: return `NULL`.
- [ ] Empty input / NULL input: return `NULL`.

## 7. Performance

- [ ] O(n) parsing per name (single‑pass, no backtracking).
- [ ] Substitution table: contiguous array, not linked list.
- [ ] Target: 100k symbols demangled in < 1 second on modern hardware.
- [ ] No global/static mutable state — fully reentrant.
- [ ] Thread‑safe: all state is stack‑local or in the output buffer.

## 8. Build System

- [ ] Create `Makefile` producing `libdemangle.a`.
- [ ] Install header to `$(DESTDIR)/usr/include/demangle.h`.
- [ ] Install library to `$(DESTDIR)/usr/lib/libdemangle.a`.
- [ ] `NATIVE_BUILD=1` support for host testing.
- [ ] No dependency on `libelfobj` — pure string processing.

## 9. Testing

### 9a. Itanium Basic Tests
- [ ] `_Z3foov` → `foo()`.
- [ ] `_Z3fooi` → `foo(int)`.
- [ ] `_ZN3Foo3barEv` → `Foo::bar()`.
- [ ] `_ZNK3Foo3barEi` → `Foo::bar(int) const`.
- [ ] `_Z3fooIiEvT_` → `void foo<int>(int)`.

### 9b. Operator Tests
- [ ] `_ZN3FooplERKS_` → `Foo::operator+(Foo const&)`.
- [ ] `_ZN3FooclEi` → `Foo::operator()(int)`.
- [ ] `_ZN3FoocvfEv` → `Foo::operator float()`.
- [ ] `_ZN3FoodlEPv` → `Foo::operator delete(void*)`.

### 9c. Constructor / Destructor Tests
- [ ] `_ZN3FooC1Ev` → `Foo::Foo()`.
- [ ] `_ZN3FooC2Ei` → `Foo::Foo(int)`.
- [ ] `_ZN3FooD0Ev` → `Foo::~Foo()`.
- [ ] `_ZN3FooD2Ev` → `Foo::~Foo()`.

### 9d. Special Name Tests
- [ ] `_ZTV3Foo` → `vtable for Foo`.
- [ ] `_ZTI3Foo` → `typeinfo for Foo`.
- [ ] `_ZTS3Foo` → `typeinfo name for Foo`.
- [ ] `_ZTT3Foo` → `VTT for Foo`.
- [ ] `_ZGVN3Foo3barE` → `guard variable for Foo::bar`.

### 9e. Template Tests
- [ ] Nested templates: `_ZN1AIiE1BIS_IjEE3fooEv` → complex nested template.
- [ ] Template literal args: `_Z3fooILi42EEvv` → `void foo<42>()`.
- [ ] Template pack: `J...E` handling.

### 9f. Substitution Tests
- [ ] Names exercising `S_`, `S0_`, `S1_` lookups.
- [ ] Standard subs: `_ZNSt6vectorIiSaIiEE...`.
- [ ] `Ss` → `std::string`.

### 9g. Type‑Only Tests (`DEMANGLE_TYPES`)
- [ ] `i` → `int`.
- [ ] `PKc` → `char const*`.
- [ ] `FvvE` → `void ()`.

### 9h. Options Tests
- [ ] `DEMANGLE_NO_PARAMS`: `_Z3fooi` → `foo`.
- [ ] `DEMANGLE_NO_VERBOSE`: `_ZNK3Foo3barEv` → `Foo::bar()` (no `const`).

### 9i. Failure / Edge Case Tests
- [ ] Non‑mangled name → `NULL`.
- [ ] Empty string → `NULL`.
- [ ] `NULL` input → `NULL`.
- [ ] Truncated `_ZN3Foo` → `NULL`.
- [ ] Deeply nested (>256 depth) → `NULL`, no crash.
- [ ] 64 KiB mangled name → completes or returns `NULL`, no crash.

### 9j. Rust v0 Basic Tests
- [ ] Crate root: `_RNvC6mycrate3foo` → `mycrate::foo`.
- [ ] Nested path: `_RNvNtC6mycrate3mod3bar` → `mycrate::mod::bar`.
- [ ] Inherent impl: `_RNvMC6mycrateNtC6mycrate3Foo3baz` → `<mycrate::Foo>::baz`.
- [ ] Trait impl: `_RNvXC6mycrateNtC6mycrate3FooNtC6mycrate5Trait3qux` → `<mycrate::Foo as mycrate::Trait>::qux`.
- [ ] Generic instantiation: `_RINvC6mycrate3fooNtC6mycrate3BarECs...` → `mycrate::foo::<mycrate::Bar>`.

### 9k. Rust v0 Type Tests
- [ ] Basic types: `l` → `i32`, `b` → `bool`, `c` → `char`, `e` → `str`.
- [ ] Reference: `R <type>` → `&T`.
- [ ] Mutable reference: `Q <type>` → `&mut T`.
- [ ] Pointer: `P <type>` → `*const T`, `O <type>` → `*mut T`.
- [ ] Array: `A <type> <const>` → `[T; N]`.
- [ ] Slice: `S <type>` → `[T]`.
- [ ] Tuple: `T <type>* E` → `(A, B)`.
- [ ] Function pointer: `F U KC <type> E <type>` → `unsafe extern "C" fn(A) -> B`.
- [ ] Trait object: `D <dyn-bounds> <lifetime>` → `dyn Trait + 'a`.

### 9l. Rust v0 Const Tests
- [ ] Integer const: type + hex value → correct decimal display.
- [ ] Negative integer: `n` prefix → negative number.
- [ ] Bool const: `0_` → `false`, `1_` → `true`.
- [ ] Char const: Unicode scalar → `'X'`.

### 9m. Rust v0 Lifetime and Binder Tests
- [ ] Erased lifetime: `L_` → `'_`.
- [ ] Bound lifetime: `L0_` → `'a`, `L1_` → `'b`.
- [ ] Higher‑ranked function pointer with lifetimes.

### 9n. Rust v0 Backref Tests
- [ ] Type backref `B <offset>` resolves correctly.
- [ ] Circular backref: returns `NULL`, no crash.
- [ ] Nested backrefs (backref to a backref).

### 9o. Rust v0 Closure / Shim Tests
- [ ] Closure: namespace `C` → `{closure#0}`.
- [ ] Shim: namespace `S` → `{shim:vtable#0}`.
- [ ] Multiple closures with disambiguators.

### 9p. Rust v0 Punycode Tests
- [ ] Unicode identifier with `u` prefix: decoded correctly.
- [ ] ASCII‑only identifier without `u` prefix: passed through.

### 9q. Rust Legacy Demangling Tests
- [ ] `_ZN6mycrate3foo17h1234567890abcdefE` → `mycrate::foo`.
- [ ] `$LT$` / `$GT$` / `$RF$` escapes → `<`, `>`, `&`.
- [ ] `$u20$` → space, `$u27$` → `'`.
- [ ] Non‑Rust `_ZN` (Itanium): not detected as Rust legacy.

### 9r. D Language Basic Tests
- [ ] `_D3std5stdio7writelnFAaZv` → `std.stdio.writeln(char[], void)` (or similar).
- [ ] Simple function: `_D3foo3barFiZi` → `foo.bar(int) → int`.
- [ ] Nested module path: `_D3std5range10primitives...`.

### 9s. D Template Instance Tests
- [ ] `_D3std5array__T5ArrayTiZ5Array...` → `std.array.Array!(int)...`.
- [ ] Template with value argument: `V` encoding.
- [ ] Template with symbol argument: `S` encoding.

### 9t. D Type Tests
- [ ] Basic types: `i` → `int`, `k` → `uint`, `f` → `float`, `a` → `char`.
- [ ] Dynamic array: `Ai` → `int[]`.
- [ ] Static array: `G3i` → `int[3]`.
- [ ] Associative array: `Hia` → `int[char]`.
- [ ] Pointer: `Pi` → `int*`.
- [ ] Delegate: `Di...` → `delegate(...)`.
- [ ] Qualifiers: `xi` → `const(int)`, `yi` → `immutable(int)`.

### 9u. D Edge Case Tests
- [ ] Non‑D `_D...` name (C symbol starting with `_D`): `DEMANGLE_DLANG` returns `NULL`, `DEMANGLE_AUTO` tries Itanium.
- [ ] D runtime symbols (`_d_*`): passed through (C mangling).
- [ ] `__lambda` / `__unittest` special sequences.

### 9v. Auto‑Detection Tests
- [ ] `_Z...` → Itanium.
- [ ] `_R...` → Rust v0.
- [ ] `_D...` → D (if Itanium fails).
- [ ] `_ZN...17h<hex>E` → Rust legacy.
- [ ] Plain `main` → `NULL` (not mangled).

### 9w. Fuzz Tests
- [ ] libFuzzer harness calling `demangle()` with arbitrary byte sequences.
- [ ] AFL harness on `c++filt` stdin mode.
- [ ] No crashes, no ASAN/UBSAN violations.

### 9x. Corpus Tests
- [ ] Demangle all symbols from `libstdc++.a` and compare against `__cxa_demangle`.
- [ ] Demangle all symbols from a large C++ project (e.g. LLVM `.o` files).

### 9y. Buffer API Tests
- [ ] `demangle_buf()` with exact‑fit buffer: returns 0.
- [ ] `demangle_buf()` with too‑small buffer: returns -2, buffer contains truncated output.
- [ ] `demangle_buf()` with 1‑byte buffer: returns -2, buffer is `'\0'`.

## 10. Documentation

- [ ] Write `demangle.3` overview man page: library purpose, header, linking, option flags, thread safety, examples.
- [ ] Write `demangle_buf.3` man page: signature, buffer semantics, return values (0 / -1 / -2), truncation behavior.
- [ ] Write `demangle_free.3` man page: signature, when to call, NULL safety.
- [ ] Write `demangle_version.3` man page: signature, return value format.
- [ ] Install all man pages to `$(DESTDIR)/usr/share/man/man3/`.
