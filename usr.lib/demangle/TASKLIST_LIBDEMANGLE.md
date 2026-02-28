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
- [ ] `<mangled-name>` → `_Z <encoding>`.
- [ ] `<encoding>` → `<name> <bare-function-type>` | `<special-name>`.
- [ ] `<name>` → `<nested-name>` | `<unscoped-name>` | `<local-name>`.
- [ ] `<nested-name>` → `N [<CV-quals>] [<ref-qual>] <prefix>+ <unqualified-name> E`.
- [ ] `<unqualified-name>` → `<source-name>` | `<operator-name>` | `<ctor-dtor-name>` | `<unnamed-type-name>`.
- [ ] `<source-name>` → `<number> <identifier>`.
- [ ] `<operator-name>` → all standard C++ operators (`nw`, `na`, `dl`, `da`, `ps`, `ng`, `ad`, `de`, `co`, `pl`, `mi`, `ml`, `dv`, `rm`, `an`, `or`, `eo`, `ls`, `rs`, `eq`, `ne`, `lt`, `gt`, `le`, `ge`, `ss`, `nt`, `pp`, `mm`, `cm`, `pm`, `pt`, `cl`, `ix`, `qu`, `cv`, `li`, `v0`–`v9`).
- [ ] `<ctor-dtor-name>` → `C1`/`C2`/`C3`/`CI1`/`CI2`, `D0`/`D1`/`D2`.
- [ ] `<template-args>` → `I <template-arg>+ E`.
- [ ] `<template-arg>` → `<type>` | `<expr-primary>` | `X <expression> E` | `J <template-arg>* E` (pack).

### 2c. Type Demangling
- [ ] Builtin types: `v`, `w`, `b`, `c`, `a`, `h`, `s`, `t`, `i`, `j`, `l`, `m`, `x`, `y`, `n`, `o`, `f`, `d`, `e`, `g`, `z`, `Dd`, `De`, `Df`, `Dh`, `Di`, `Ds`, `Da`, `Dc`, `Dn`.
- [ ] `<pointer-type>` (`P`), `<reference-type>` (`R`), `<rvalue-reference>` (`O`).
- [ ] `<complex-type>` (`C`), `<imaginary-type>` (`G`).
- [ ] CV qualifiers (`K`, `V`, `r`) applied to types.
- [ ] `<function-type>` → `F [Y] <type>+ E` (with optional extern "C" marker `Y`).
- [ ] `<array-type>` → `A <number> _ <type>`.
- [ ] `<pointer-to-member>` → `M <type> <type>`.
- [ ] `<template-param>` → `T_` | `T <seq-id> _`.
- [ ] `<decltype>` → `Dt` / `DT`.
- [ ] Vendor‑extended type → `u <source-name>`.

### 2d. Substitutions
- [ ] Build substitution table during parse.
- [ ] `S_`, `S <seq-id> _` lookups.
- [ ] Standard substitutions: `St` (std), `Sa` (std::allocator), `Sb` (std::basic_string), `Ss` (std::string), `Si` (std::istream), `So` (std::ostream), `Sd` (std::iostream).

### 2e. Special Names
- [ ] `TV <type>` — vtable.
- [ ] `TT <type>` — VTT.
- [ ] `TI <type>` — typeinfo.
- [ ] `TS <type>` — typeinfo name.
- [ ] `GV <name>` — guard variable.
- [ ] `Tc`/`Th`/`Tv` — thunks (with offset parsing).
- [ ] `T` — transaction clones (deferred).

### 2f. Expressions and Literals
- [ ] Integer literals: `L <type> <number> E`.
- [ ] Nullptr: `LDnE`.
- [ ] Expression operators: `cl`, `ix`, `qu`, `st`, `sz`, `at`, `az`, etc.
- [ ] Cast expressions: `cv <type> <expr>`.
- [ ] Fold expressions (C++17): `fL`, `fR`, `fl`, `fr`.
- [ ] Pack expansion: `sp`.

### 2g. Lambda / Closure Types
- [ ] `Ul <type>* E [<number>] _`.

## 3. Rust Demangler (secondary)

- [ ] Detect `_R` prefix.
- [ ] Parse Rust v0 mangling: `<path>`, `<impl-path>`, `<type>`, `<const>`.
- [ ] Handle crate disambiguators and closure/shim names.
- [ ] Output: `crate::module::function` style.
- [ ] Fallback: output original on parse failure.

## 4. D Language Demangler (tertiary, defer if not needed)

- [ ] Detect `_D` prefix.
- [ ] Parse D mangling scheme (LName‑based).
- [ ] Defer unless toolchain needs it.

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

### 9j. Rust Tests (when implemented)
- [ ] `_RNvCs...` → Rust v0 path.
- [ ] Non‑Rust `_R...` → `NULL`.

### 9k. Fuzz Tests
- [ ] libFuzzer harness calling `demangle()` with arbitrary byte sequences.
- [ ] AFL harness on `c++filt` stdin mode.
- [ ] No crashes, no ASAN/UBSAN violations.

### 9l. Corpus Tests
- [ ] Demangle all symbols from `libstdc++.a` and compare against `__cxa_demangle`.
- [ ] Demangle all symbols from a large C++ project (e.g. LLVM `.o` files).

### 9m. Buffer API Tests
- [ ] `demangle_buf()` with exact‑fit buffer: returns 0.
- [ ] `demangle_buf()` with too‑small buffer: returns -2, buffer contains truncated output.
- [ ] `demangle_buf()` with 1‑byte buffer: returns -2, buffer is `'\0'`.

## 10. Documentation

- [ ] Write `demangle.3` overview man page: library purpose, header, linking, option flags, thread safety, examples.
- [ ] Write `demangle_buf.3` man page: signature, buffer semantics, return values (0 / -1 / -2), truncation behavior.
- [ ] Write `demangle_free.3` man page: signature, when to call, NULL safety.
- [ ] Write `demangle_version.3` man page: signature, return value format.
- [ ] Install all man pages to `$(DESTDIR)/usr/share/man/man3/`.
