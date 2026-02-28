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

- [x] `demangle()` returns `NULL` for unrecognized input — output original string unchanged.
- [x] Truncated mangled name: output original unchanged.
- [x] Library handles recursion depth guards internally — no crash.
- [x] Malformed grammar: output original unchanged, no crash.
- [x] Never exit non‑zero for demangling failures — failures are expected for non‑C++ input.

---

## 4. Flags

- [x] `-s <style>`: demangling style (see §2).
- [x] `-n` / `--no-strip-underscore`: don't strip leading `_`.
- [x] `-_` / `--strip-underscore`: strip leading `_`.
- [x] `-p` / `--no-params`: pass `DEMANGLE_NO_PARAMS` to `demangle()`.
- [x] `-t` / `--types`: pass `DEMANGLE_TYPES` to `demangle()`.
- [x] `-i` / `--no-verbose`: pass `DEMANGLE_NO_VERBOSE` to `demangle()`.
- [x] `--help` / `-h`: usage.
- [x] `--version` / `-V`: print version (use `demangle_version()`).

---

## 5. Integration with Other Tools

`libdemangle.a` is already built under `usr.lib/demangle/`. Other tools link against it:

- [x] `nm -C`: call `demangle()` on each symbol name.
- [x] `objdump -C`: call `demangle()` on disassembly labels.
- [x] `addr2line -C`: call `demangle()` on function names.

> These belong to the respective tool tasklists. Listed here for
> cross‑reference only.

---

## 6. Error Handling

- [x] Stdin read error: report to stderr, exit 1.
- [x] Invalid `-s` style: error message, exit 1.
- [x] All other errors (bad names): pass through, exit 0.

---

## 7. Build System

- [x] Create `Makefile` in `usr.bin/cxxfilt/`.
- [x] Link against `usr.lib/demangle/libdemangle.a` and libc.
- [x] No `libelfobj` dependency (pure string processing via `libdemangle`).
- [x] `NATIVE_BUILD=1` support (link against host libc + `libdemangle` built for host).
- [x] `install` to `$(DESTDIR)/usr/bin/c++filt`.

---

## 8. Testing

### 8a. CLI Smoke Tests
- [x] `echo '_ZN3Foo3barEv' | c++filt` → `Foo::bar()`.
- [x] `echo '_Z3fooi' | c++filt` → `foo(int)`.
- [x] `c++filt _ZN9Wikipedia7articleE` → `Wikipedia::article`.
- [x] `c++filt _Z1fv` → `f()`.
- [x] `echo '_ZNK3Foo3barEi' | c++filt` → `Foo::bar(int) const`.

### 8b. Stdin Streaming Tests
- [x] Mixed mangled/unmangled text: only `_Z`/`_R`/`_D` tokens demangled.
- [x] Multiple mangled names per line.
- [x] Empty input: empty output.
- [x] Lines without any mangled names: pass through unchanged.

### 8c. Argv Mode Tests
- [x] Single argument demangling.
- [x] Multiple arguments, one per output line.

### 8d. Flag Tests
- [x] `-p`: `c++filt -p _Z3fooi` → `foo` (no params).
- [x] `-n`: `c++filt -n __Z3fooi` → `foo(int)` (strip underscore off).
- [x] `-s none`: all names pass through unchanged.
- [x] `-s rust`: Rust symbols demangled, C++ symbols passed through.
- [x] `-s dlang`: D symbols demangled, C++ symbols passed through.
- [x] `-t`: bare type strings demangled (e.g. `i` → `int`).
- [x] `-V`: prints `demangle_version()` output.

### 8e. Failure / Edge‑Case Tests
- [x] Non‑mangled name: passed through unchanged.
- [x] Truncated name (`_ZN3Foo`): passed through unchanged.
- [x] Random binary data: no crash, passed through.
- [x] Invalid `-s` argument: error message, exit 1.

### 8f. Integration Tests
- [x] `nm -C` output matches `c++filt` output on the same binary.
- [x] `objdump -C -d` labels match `c++filt` output.

### 8g. Large Corpus Tests
- [x] Pipe all symbols from a real C++ library through `c++filt`.
- [x] Compare output against host `c++filt` or `__cxa_demangle`.

> **Note:** unit tests for the demangling engine itself live in
> `tests/usr.lib/demangle/`. The tests above exercise only the CLI
> wrapper and its flag/IO behavior.

---

## 9. Man Page

- [x] Write `c++filt.1` covering all flags, styles, and input modes.
- [x] Document supported mangling schemes (Itanium, Rust, D-lang) and reference `demangle(3)`.
- [x] Install to `$(DESTDIR)/usr/share/man/man1/`.
