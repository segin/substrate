# `usr.bin/cxxfilt` Tasklist

Goal: implement `c++filt`‑compatible demangler utility.

---

## 1. Itanium ABI Demangling Engine

### 1a. Core Grammar (Itanium C++ ABI §5.1)
- [ ] `<mangled-name>` → `_Z <encoding>`.
- [ ] `<encoding>` → `<function name>` | `<special-name>` | `<data name>`.
- [ ] `<name>` → `<nested-name>` | `<unscoped-name>` | `<local-name>`.

### 1b. Name Components
- [ ] `<unqualified-name>` → `<source-name>` | `<operator-name>` | `<ctor-dtor-name>`.
- [ ] `<source-name>` → `<length> <identifier>`.
- [ ] `<nested-name>` → `N [<CV-qualifiers>] [<ref-qualifier>] <prefix> <unqualified-name> E`.
- [ ] `<template-prefix>` and `<template-args>` → `I <template-arg>+ E`.
- [ ] `<substitution>` → `S_` | `S <seq-id> _` | standard substitutions (`St`, `Sa`, `Sb`, `Ss`, `Si`, `So`).
- [ ] `<decltype>` → `Dt <expression> E` | `DT <expression> E`.

### 1c. Types
- [ ] Builtin types: `v` (void), `b` (bool), `c` (char), `i` (int), `l` (long), `d` (double), `f` (float), `e` (long double), etc.
- [ ] `<pointer-type>` → `P <type>`.
- [ ] `<reference-type>` → `R <type>`.
- [ ] `<rvalue-reference>` → `O <type>`.
- [ ] `<CV-qualified-type>` → `K` (const), `V` (volatile), `r` (restrict) + `<type>`.
- [ ] `<function-type>` → `F [Y] <type>+ E`.
- [ ] `<array-type>` → `A <number> _ <type>` | `A <expression> _ <type>`.
- [ ] `<template-param>` → `T_` | `T <number> _`.
- [ ] Vendor extended types: `u <source-name>`.

### 1d. Operator Names
- [ ] All standard operators: `nw` (new), `na` (new[]), `dl` (delete), `da` (delete[]), `ps` (+), `ng` (-), `ad` (&), `de` (*), `co` (~), `pl` (+), `mi` (-), `ml` (*), `dv` (/), `rm` (%), etc.
- [ ] Conversion operators: `cv <type>`.
- [ ] Vendor extended operators: `v<digit> <source-name>`.
- [ ] Literal operator: `li <source-name>`.

### 1e. Constructor/Destructor Names
- [ ] `C1` (complete constructor), `C2` (base constructor), `C3` (allocating constructor).
- [ ] `D0` (deleting destructor), `D1` (complete destructor), `D2` (base destructor).

### 1f. Special Names
- [ ] `TV` (vtable), `TT` (VTT), `TI` (typeinfo), `TS` (typeinfo name).
- [ ] `GV` (guard variable).
- [ ] `Th`/`Tv` (thunks with offsets).
- [ ] `T` (transaction‑safe clones, deferred).

### 1g. Expressions and Literals
- [ ] Integer literals: `L <type> <value> E`.
- [ ] String literals (deferred).
- [ ] Expression operators: `cl` (call), `ix` (index), `qu` (ternary), etc.
- [ ] Expression folding/pack expansions (C++17): `fL`/`fR`/`fl`/`fr`.

### 1h. Lambda and Closure Types
- [ ] `Ul <type>* E <number> _` (unnamed closure type).

### 1i. Substitution Table
- [ ] Build substitution table during parsing.
- [ ] `S_` = first substitution, `S0_` = second, etc.
- [ ] Standard substitutions: `St` → `std::`, `Sa` → `std::allocator`, `Sb` → `std::basic_string`, `Ss` → `std::string`, `Si` → `std::istream`, `So` → `std::ostream`.

## 2. Input / Output Modes

### 2a. Stdin Streaming (default)
- [ ] Read stdin line by line.
- [ ] For each line: scan for mangled names (`_Z...`), demangle in‑place, output line.
- [ ] Non‑mangled text passes through unchanged.
- [ ] Handle names embedded in larger tokens (e.g. `_ZN3Foo3barEv:` — demangle up to non‑name char).

### 2b. Argv Mode
- [ ] Arguments on command line are individual mangled names.
- [ ] Print demangled name per line.
- [ ] If no arguments and no stdin: read stdin.

### 2c. Whole‑Symbol vs Embedded
- [ ] Default: scan for `_Z` prefixed tokens in each line, demangle only those.
- [ ] `--no-strip-underscore` / `-n`: do not strip leading `_` before demangling.
- [ ] `--strip-underscore` / `-_`: strip one leading underscore (for systems that prepend `_`).

## 3. Demangling Styles (`-s <style>`)

- [ ] `auto` (default): try Itanium, fall back to literal.
- [ ] `gnu-v3` / `itanium`: Itanium ABI only.
- [ ] `none`: pass‑through (no demangling).
- [ ] Future: `rust` (Rust `_R` mangling), `dlang` (D `_D` mangling) — defer.

## 4. Failure Behavior

- [ ] Unrecognized mangled name: output original string unchanged.
- [ ] Truncated mangled name: output original unchanged.
- [ ] Deeply nested templates (>256 depth): output original unchanged, no crash (guard recursion).
- [ ] Malformed grammar: output original unchanged, no crash.
- [ ] Never exit non‑zero for demangling failures — failures are expected for non‑C++ input.

## 5. Flags

- [ ] `-s <style>`: demangling style.
- [ ] `-n` / `--no-strip-underscore`: don't strip leading `_`.
- [ ] `-_` / `--strip-underscore`: strip leading `_`.
- [ ] `-p` / `--no-params`: omit function parameter types (show only name).
- [ ] `-t` / `--types`: attempt to demangle types as well as names.
- [ ] `-i` / `--no-verbose`: suppress verbose qualifiers (`const`, `volatile`).
- [ ] `-r` / `--no-recurse-limit`: remove recursion depth limit (use with caution).
- [ ] `--help` / `-h`: usage.
- [ ] `--version` / `-V`: version.

## 6. Integration with Other Tools

- [ ] Provide a `demangle()` function in a shared header or small `libdemangle.a`.
- [ ] `nm -C` calls `demangle()` on each symbol name.
- [ ] `objdump -C` calls `demangle()` on disassembly labels.
- [ ] `addr2line -C` calls `demangle()` on function names.
- [ ] Library API: `char *cxxfilt_demangle(const char *mangled)` → heap‑allocated string, caller frees.

## 7. Error Handling

- [ ] Stdin read error: report, exit 1.
- [ ] Invalid `-s` style: error, exit 1.
- [ ] All other errors (bad names): pass through, exit 0.

## 8. Performance

- [ ] O(n) parsing per mangled name (single pass, no backtracking).
- [ ] Substitution table: dynamic array, not linked list.
- [ ] Handle names up to 64 KiB without allocation failure.
- [ ] Benchmark: demangle 100k symbols in < 1s.

## 9. Build System

- [ ] Create `Makefile` — no `libelfobj` dependency (pure string processing).
- [ ] Optionally build `libdemangle.a` for use by `nm`/`objdump`/`addr2line`.
- [ ] `NATIVE_BUILD=1` support.
- [ ] `install` to `$(DESTDIR)/usr/bin/c++filt`.

## 10. Testing

### 10a. Basic Demangling Tests
- [ ] `_ZN3Foo3barEv` → `Foo::bar()`.
- [ ] `_Z3fooi` → `foo(int)`.
- [ ] `_ZN9Wikipedia7articleE` → `Wikipedia::article`.
- [ ] `_Z1fv` → `f()`.
- [ ] `_ZNK3Foo3barEi` → `Foo::bar(int) const`.

### 10b. Template Tests
- [ ] `_Z3fooIiEvT_` → `void foo<int>(int)`.
- [ ] `_ZN3FooIiE3barEdT_` → `Foo<int>::bar(double, int)`.
- [ ] Nested templates: `_Z3fooILi42EEvv` → `void foo<42>()`.
- [ ] Multiple template args.

### 10c. Operator Tests
- [ ] `_ZN3FooplERKS_` → `Foo::operator+(Foo const&)`.
- [ ] `_ZN3FooclEi` → `Foo::operator()(int)`.
- [ ] `_ZN3FoocvfEv` → `Foo::operator float()`.

### 10d. Constructor/Destructor Tests
- [ ] `_ZN3FooC1Ev` → `Foo::Foo()`.
- [ ] `_ZN3FooD0Ev` → `Foo::~Foo()`.

### 10e. Special Name Tests
- [ ] `_ZTV3Foo` → `vtable for Foo`.
- [ ] `_ZTI3Foo` → `typeinfo for Foo`.
- [ ] `_ZTS3Foo` → `typeinfo name for Foo`.
- [ ] `_ZGVN3Foo3barE` → `guard variable for Foo::bar`.

### 10f. Substitution Tests
- [ ] Names reusing substitutions from earlier components.
- [ ] Standard substitutions: `St`, `Sa`, `Ss`.

### 10g. Failure Tests
- [ ] Non‑mangled name: passed through unchanged.
- [ ] Truncated name (`_ZN3Foo`): passed through unchanged.
- [ ] Extremely deeply nested name (>256 depth): no crash, passed through.
- [ ] Random binary data (fuzz): no crash.

### 10h. Streaming Tests
- [ ] Stdin with mixed mangled/unmangled text: only `_Z...` tokens demangled.
- [ ] Multiple names per line.
- [ ] Empty input: empty output.

### 10i. Large Corpus Tests
- [ ] Demangle all symbols from a real C++ library (e.g. libstdc++, libc++).
- [ ] Compare output against `__cxa_demangle` or host `c++filt`.

### 10j. Flag Tests
- [ ] `-p`: `_Z3fooi` → `foo` (no params).
- [ ] `-n`: `__Z3fooi` → `foo(int)` (strip underscore off).
- [ ] `-s none`: all names pass through unchanged.

### 10k. Integration Tests
- [ ] `nm -C` calls library demangler — verify demangled output.
- [ ] `objdump -C -d` calls library demangler — verify labels.

## 11. Man Page

- [ ] Write `c++filt.1` covering all flags, styles, and input modes.
- [ ] Document supported mangling schemes.
- [ ] Install to `$(DESTDIR)/usr/share/man/man1/`.
