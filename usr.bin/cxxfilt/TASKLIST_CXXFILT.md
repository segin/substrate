# `usr.bin/cxxfilt` Tasklist

Goal: implement `c++filt`‑compatible demangler utility, using `usr.lib/demangle/` (`libdemangle.a`) for all demangling logic.

> **Dependency:** all demangling is performed by `libdemangle`
> (`include/demangle.h`).  The library already provides Itanium C++,
> Rust (`_R`), and D-lang (`_D`) demangling with option flags:
>
> | Flag                   | Effect                              |
> |------------------------|-------------------------------------|
> | `DEMANGLE_AUTO`        | Auto-detect scheme (default)        |
> | `DEMANGLE_ITANIUM`     | Force Itanium ABI                   |
> | `DEMANGLE_RUST`        | Force Rust v0 mangling              |
> | `DEMANGLE_DLANG`       | Force D-lang mangling               |
> | `DEMANGLE_NO_PARAMS`   | Omit function parameter types       |
> | `DEMANGLE_NO_VERBOSE`  | Suppress `const`/`volatile`         |
> | `DEMANGLE_TYPES`       | Attempt to demangle bare type names |
>
> See `usr.lib/demangle/` and `tests/usr.lib/demangle/` for the library
> implementation and its own unit/fuzz/perf tests.

---

## 1. Input / Output Modes

### 1a. Stdin Streaming (default)
- [x] Read stdin line by line.
- [x] For each line: scan for mangled tokens (`_Z...`, `_R...`, `_D...`), call `demangle()` on each, replace in‑place, output line.
- [x] Non‑mangled text passes through unchanged.
- [x] Handle names embedded in larger tokens (e.g. `_ZN3Foo3barEv:` — demangle up to non‑name char).

### 1b. Argv Mode
- [x] Arguments on command line are individual mangled names.
- [x] Print demangled name per line.
- [x] If no arguments and no stdin: read stdin.

### 1c. Whole‑Symbol vs Embedded
- [x] Default: scan for `_Z`/`_R`/`_D` prefixed tokens in each line, demangle only those.
- [x] `--no-strip-underscore` / `-n`: do not strip leading `_` before demangling.
- [x] `--strip-underscore` / `-_`: strip one leading underscore (for systems that prepend `_`).

---

## 2. Demangling Styles (`-s <style>`)

Map CLI style names to `libdemangle` option flags:

| `-s` value              | Flag                |
|--------------------------|---------------------|
| `auto` (default)         | `DEMANGLE_AUTO`     |
| `gnu-v3` / `itanium`    | `DEMANGLE_ITANIUM`  |
| `rust`                   | `DEMANGLE_RUST`     |
| `dlang`                  | `DEMANGLE_DLANG`    |
| `none`                   | pass‑through (skip) |

- [x] Parse `-s <style>` and convert to the appropriate `DEMANGLE_*` flag.
- [x] `none` style: output all names unchanged (no `demangle()` call).

---

## 3. Failure Behavior

- [ ] `demangle()` returns `NULL` for unrecognized input — output original string unchanged.
- [ ] Truncated mangled name: output original unchanged.
- [ ] Library handles recursion depth guards internally — no crash.
- [ ] Malformed grammar: output original unchanged, no crash.
- [ ] Never exit non‑zero for demangling failures — failures are expected for non‑C++ input.

---

## 4. Flags

- [ ] `-s <style>`: demangling style (see §2).
- [ ] `-n` / `--no-strip-underscore`: don't strip leading `_`.
- [ ] `-_` / `--strip-underscore`: strip leading `_`.
- [ ] `-p` / `--no-params`: pass `DEMANGLE_NO_PARAMS` to `demangle()`.
- [ ] `-t` / `--types`: pass `DEMANGLE_TYPES` to `demangle()`.
- [ ] `-i` / `--no-verbose`: pass `DEMANGLE_NO_VERBOSE` to `demangle()`.
- [ ] `--help` / `-h`: usage.
- [ ] `--version` / `-V`: print version (use `demangle_version()`).

---

## 5. Integration with Other Tools

`libdemangle.a` is already built under `usr.lib/demangle/`. Other tools link against it:

- [ ] `nm -C`: call `demangle()` on each symbol name.
- [ ] `objdump -C`: call `demangle()` on disassembly labels.
- [ ] `addr2line -C`: call `demangle()` on function names.

> These belong to the respective tool tasklists. Listed here for
> cross‑reference only.

---

## 6. Error Handling

- [ ] Stdin read error: report to stderr, exit 1.
- [ ] Invalid `-s` style: error message, exit 1.
- [ ] All other errors (bad names): pass through, exit 0.

---

## 7. Build System

- [ ] Create `Makefile` in `usr.bin/cxxfilt/`.
- [ ] Link against `usr.lib/demangle/libdemangle.a` and libc.
- [ ] No `libelfobj` dependency (pure string processing via `libdemangle`).
- [ ] `NATIVE_BUILD=1` support (link against host libc + `libdemangle` built for host).
- [ ] `install` to `$(DESTDIR)/usr/bin/c++filt`.

---

## 8. Testing

### 8a. CLI Smoke Tests
- [ ] `echo '_ZN3Foo3barEv' | c++filt` → `Foo::bar()`.
- [ ] `echo '_Z3fooi' | c++filt` → `foo(int)`.
- [ ] `c++filt _ZN9Wikipedia7articleE` → `Wikipedia::article`.
- [ ] `c++filt _Z1fv` → `f()`.
- [ ] `echo '_ZNK3Foo3barEi' | c++filt` → `Foo::bar(int) const`.

### 8b. Stdin Streaming Tests
- [ ] Mixed mangled/unmangled text: only `_Z`/`_R`/`_D` tokens demangled.
- [ ] Multiple mangled names per line.
- [ ] Empty input: empty output.
- [ ] Lines without any mangled names: pass through unchanged.

### 8c. Argv Mode Tests
- [ ] Single argument demangling.
- [ ] Multiple arguments, one per output line.

### 8d. Flag Tests
- [ ] `-p`: `c++filt -p _Z3fooi` → `foo` (no params).
- [ ] `-n`: `c++filt -n __Z3fooi` → `foo(int)` (strip underscore off).
- [ ] `-s none`: all names pass through unchanged.
- [ ] `-s rust`: Rust symbols demangled, C++ symbols passed through.
- [ ] `-s dlang`: D symbols demangled, C++ symbols passed through.
- [ ] `-t`: bare type strings demangled (e.g. `i` → `int`).
- [ ] `-V`: prints `demangle_version()` output.

### 8e. Failure / Edge‑Case Tests
- [ ] Non‑mangled name: passed through unchanged.
- [ ] Truncated name (`_ZN3Foo`): passed through unchanged.
- [ ] Random binary data: no crash, passed through.
- [ ] Invalid `-s` argument: error message, exit 1.

### 8f. Integration Tests
- [ ] `nm -C` output matches `c++filt` output on the same binary.
- [ ] `objdump -C -d` labels match `c++filt` output.

### 8g. Large Corpus Tests
- [ ] Pipe all symbols from a real C++ library through `c++filt`.
- [ ] Compare output against host `c++filt` or `__cxa_demangle`.

> **Note:** unit tests for the demangling engine itself live in
> `tests/usr.lib/demangle/`. The tests above exercise only the CLI
> wrapper and its flag/IO behavior.

---

## 9. Man Page

- [ ] Write `c++filt.1` covering all flags, styles, and input modes.
- [ ] Document supported mangling schemes (Itanium, Rust, D-lang) and reference `demangle(3)`.
- [ ] Install to `$(DESTDIR)/usr/share/man/man1/`.
