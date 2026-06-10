# 24. `libm` Math Library Completion

> **Audit basis.** This tasklist was produced from a symbol-level audit of
> substrate's `lib/m/libm.a` / `libm.so.0` against the declarations in
> `include/math.h`, `include/complex.h`, and `include/fenv.h`, cross-checked
> against `lib/c/libc.a` (the classification/`__signbit`/`__fpclassify`/
> `__isnan`/`__isinf`/`__issignaling`/`__iseqsig` backing helpers and the
> `signgam` global already live in libc, so the `fpclassify`/`isnan`/`isinf`/
> `isfinite`/`isnormal`/`signbit`/`issignaling`/`iseqsig` macros already link).
> libm is otherwise broad: it already ships the full real, long-double, complex,
> Bessel, gamma/erf, rounding, `fenv`, and C23 `fmaximum`/`fminimum`/`fromfp`/
> `nextup`/`compound`/`pown`/`rootn`/`*pi` surfaces.  The gaps below are the
> remaining standard, obsolescent-but-still-referenced, and C23 functions that
> are **not defined anywhere** in libm or libc.

> **Greenfield additions only.**  No live link breakage exists today (the
> headers do not declare the missing symbols), so this is a *completeness*
> effort: add each function family, declare it in the appropriate header behind
> the correct feature-test macro, and prove it with tests and a manual page.
> Builds on the existing `lib/m/src/` layout (`math_*.c`, `mathf.c`, `mathl.c`,
> `cmath_*.c`, `fenv.c`) and the libm test/man conventions established in
> [`06-6-c-library.md`](06-6-c-library.md) §6 and Directive 9.

## Overview

Five independently-shippable function classes remain missing from `-lm`.
Each class is a self-contained slice with its own header surface, test suite,
and manual pages; later classes do not depend on earlier ones, so they may be
implemented in any order.

| § | Class | Functions | Standard |
|---|-------|-----------|----------|
| 24.2 | Obsolescent aliases | `scalb`, `scalbf`, `significand{,f,l}`, `drem`, `dremf`, `gamma`, `gammaf`, `pow10{,f,l}`, `matherr` | SVID / 4.3BSD / X/Open (obsolescent) |
| 24.3 | Narrowing arithmetic | `f{add,sub,mul,div,fma,sqrt}`, `f*l`, `d{add,sub,mul,div,fma,sqrt}`, `d*l` | ISO C23 §7.12.14 |
| 24.4 | Total order & NaN payload | `totalorder{,f,l}`, `totalordermag{,f,l}`, `canonicalize{,f,l}`, `getpayload{,f,l}`, `setpayload{,f,l}`, `setpayloadsig{,f,l}` | ISO C23 §7.12.11/§F.10.x (IEEE 754-2019) |
| 24.5 | Reentrant gamma (`f`/`l`) | `lgammaf_r`, `lgammal_r` | glibc / 4.3BSD-Reno extension |
| 24.6 | Complex base-10 logarithm | `clog10`, `clog10f`, `clog10l` | glibc extension |

The double-precision `lgamma_r`, the long-double `scalbl`, and the
classification/comparison macro backing already exist and are out of scope.

## Reimplemented Checklist (All Open)

### 24.1 Audit, Build Wiring & Header Surface

- [ ] **Symbol audit fixture:** add a host test that links a probe object
      referencing every symbol this tasklist adds and asserts each resolves,
      so the completion of each class is mechanically verifiable. (REQ: REQ-24-0001)
- [ ] **Source placement:** add new translation units under `lib/m/src/`
      (`math_narrowing.c`, `math_totalorder.c`, `math_payload.c`, plus
      additions to `math_special.c`/`mathf.c`/`mathl.c`/`cmath_explog.c`),
      compiled in both the `.o` and `.pic.o` passes per the existing dual-build
      `Makefile`. (REQ: REQ-24-0002)
- [ ] **`libm.a` + `libm.so.0`:** ensure every new object is archived into the
      static lib and linked into the shared lib, with the OSABI byte preserved
      by the existing build step. (REQ: REQ-24-0003)
- [ ] **Header declarations:** declare each new function in the correct header
      (`math.h` / `complex.h`) under the correct feature-test guard
      (`__STDC_VERSION__ >= 202311L` for C23 classes; `_GNU_SOURCE` /
      `_XOPEN_SOURCE` / `_DEFAULT_SOURCE` for the obsolescent and glibc-extension
      classes), with no clashing prototypes for the symbols already present. (REQ: REQ-24-0004)
- [ ] **No regressions:** confirm `make -C lib/m` and a representative link of
      an existing libm consumer (`bin/top`, the libm test suite) still build. (REQ: REQ-24-0005)

### 24.2 Obsolescent Aliases (SVID / BSD / X/Open)

- [ ] **`scalb` / `scalbf`:** the obsolescent two-`double`/`float`-argument
      `scalb(x, n)` returning `x * 2**n` (companion to the existing `scalbl`).
      Define in terms of the already-present `scalbn`/`ldexp` machinery, matching
      the historical "fractional exponent" edge semantics where `n` is `±Inf`. (REQ: REQ-24-0006)
- [ ] **`significand{,f,l}`:** return the mantissa of `x` scaled to `[1, 2)`
      (equivalently `scalb(x, -ilogb(x))`); defined for finite non-zero `x`. (REQ: REQ-24-0007)
- [ ] **`drem` / `dremf`:** obsolescent aliases of `remainder` / `remainderf`
      (IEEE round-to-nearest remainder); implement as thin forwarders. (REQ: REQ-24-0008)
- [ ] **`gamma` / `gammaf`:** the historical name for `lgamma` / `lgammaf`
      (log-gamma, **not** `tgamma`); forward and set `signgam`. (REQ: REQ-24-0009)
- [ ] **`pow10{,f,l}`:** aliases of the existing `exp10{,f,l}` (base-10
      exponential); thin forwarders. (REQ: REQ-24-0010)
- [ ] **`matherr` (SVID error hook):** provide the default `int matherr(struct
      exception *)` returning 0 (no override), and document that substrate libm
      reports errors via `errno`/`fenv` rather than the SVID hook so callers can
      still link SVID-era code. (REQ: REQ-24-0011)
- [ ] **Guarding:** expose this family only under `_DEFAULT_SOURCE` /
      `_XOPEN_SOURCE` (obsolescent), and mark each as deprecated in the header
      comment pointing at the standard replacement. (REQ: REQ-24-0012)

### 24.3 ISO C23 Narrowing Arithmetic (§7.12.14)

> Each function performs one IEEE operation on wider operands and rounds the
> infinitely-precise result **once** to the narrower return type.  `f`-prefixed
> return `float`; `d`-prefixed return `double`; the suffix denotes the wider
> argument type (`fadd`: `double`→`float`; `faddl`: `long double`→`float`;
> `daddl`: `long double`→`double`).

- [ ] **Addition:** `fadd`, `faddl`, `dadd`, `daddl` — correctly-rounded
      narrowing sum (single rounding, not add-then-cast). (REQ: REQ-24-0013)
- [ ] **Subtraction:** `fsub`, `fsubl`, `dsub`, `dsubl`. (REQ: REQ-24-0014)
- [ ] **Multiplication:** `fmul`, `fmull`, `dmul`, `dmull`. (REQ: REQ-24-0015)
- [ ] **Division:** `fdiv`, `fdivl`, `ddiv`, `ddivl`. (REQ: REQ-24-0016)
- [ ] **Fused multiply-add:** `ffma`, `ffmal`, `dfma`, `dfmal` — narrowing of
      the exact `x*y + z`, built on the existing `fma`/`fmal`. (REQ: REQ-24-0017)
- [ ] **Square root:** `fsqrt`, `fsqrtl`, `dsqrt`, `dsqrtl`. (REQ: REQ-24-0018)
- [ ] **Rounding & flags:** each narrowing op SHALL honour the current rounding
      mode for the single final rounding and raise the correct `fenv`
      exceptions (`inexact`, `overflow`, `underflow`, `invalid`, `divbyzero`). (REQ: REQ-24-0019)
- [ ] **Single-rounding correctness:** verify the narrowing result differs from
      the naïve `(float)(a OP b)` double-rounding on at least one chosen-witness
      input per operation. (REQ: REQ-24-0020)
- [ ] **Header surface:** declare under `__STDC_VERSION__ >= 202311L` in
      `math.h`. (REQ: REQ-24-0021)

### 24.4 ISO C23 Total Order & NaN Payload (IEEE 754-2019)

- [ ] **`totalorder{,f,l}`:** the IEEE 754 `totalOrder` predicate — a total
      ordering over all representable values including signed zeros and all
      NaN encodings (returns nonzero iff `x` precedes-or-equals `y`). (REQ: REQ-24-0022)
- [ ] **`totalordermag{,f,l}`:** `totalOrder` on `|x|` and `|y|`. (REQ: REQ-24-0023)
- [ ] **`canonicalize{,f,l}`:** produce the canonical encoding of `x` into
      `*cx`, returning 0 on success (nonzero for a non-canonical/signaling
      input that cannot be canonicalized); for binary formats this copies `x`
      and quiets sNaN, returning the appropriate status. (REQ: REQ-24-0024)
- [ ] **`getpayload{,f,l}`:** return the NaN payload of `*x` as a floating value
      (or `-1` when `*x` is not a NaN), per IEEE `getPayload`. (REQ: REQ-24-0025)
- [ ] **`setpayload{,f,l}`:** set `*res` to a **quiet** NaN carrying the integer
      payload `pl`, returning 0 on success and nonzero (with `*res = +0`) when
      `pl` is not a valid payload. (REQ: REQ-24-0026)
- [ ] **`setpayloadsig{,f,l}`:** as `setpayload` but producing a **signaling**
      NaN. (REQ: REQ-24-0027)
- [ ] **Encoding correctness:** payload get/set SHALL round-trip across the
      mantissa width of each type (`float` 23-bit, `double` 52-bit, `long double`
      64-bit explicit-integer-bit i387 format) and reject out-of-range payloads. (REQ: REQ-24-0028)
- [ ] **No spurious exceptions:** `totalorder*`, `totalordermag*`, and
      `getpayload*` SHALL be quiet (raise no `fenv` exceptions) even for
      signaling-NaN inputs. (REQ: REQ-24-0029)
- [ ] **Header surface:** declare under `__STDC_VERSION__ >= 202311L` in
      `math.h`. (REQ: REQ-24-0030)

### 24.5 Reentrant Gamma (`float` / `long double`)

- [ ] **`lgammaf_r(float, int *signp)` / `lgammal_r(long double, int *signp)`:**
      thread-safe log-gamma returning the sign of Γ via `*signp` instead of the
      global `signgam` (companions to the existing double `lgamma_r`); reuse the
      existing `lgammaf`/`lgammal` cores. (REQ: REQ-24-0031)
- [ ] **`signgam` independence:** the `_r` variants SHALL NOT read or write the
      global `signgam`. (REQ: REQ-24-0032)
- [ ] **Header surface:** declare under `_GNU_SOURCE` / `_DEFAULT_SOURCE` in
      `math.h`. (REQ: REQ-24-0033)

### 24.6 Complex Base-10 Logarithm

- [ ] **`clog10`, `clog10f`, `clog10l`:** complex base-10 logarithm
      (`clog(z) / ln(10)`), with branch cut along the negative real axis matching
      the existing `clog` family. (REQ: REQ-24-0034)
- [ ] **Header surface:** declare under `_GNU_SOURCE` in `complex.h`. (REQ: REQ-24-0035)

### 24.7 Testing

- [ ] **Unit tests** for every new symbol with reference vectors (golden values
      from a trusted host libm at the same precision), placed under
      `tests/lib/m/` per the project test layout. (REQ: REQ-24-0036)
- [ ] **Edge-case tests:** `±0`, `±Inf`, qNaN, sNaN, subnormals, and the
      largest/smallest finite magnitudes for each type and each function. (REQ: REQ-24-0037)
- [ ] **Property tests:** narrowing ops vs. arbitrary-precision single-rounding
      oracle; `totalorder` antisymmetry/transitivity/totality; payload
      get/set round-trip; `clog10` vs. `clog(z)/log(10)` within tolerance. (REQ: REQ-24-0038)
- [ ] **`fenv` interaction tests:** assert the exception flags raised (and not
      raised) for the narrowing and total-order/payload classes under each
      rounding mode. (REQ: REQ-24-0039)
- [ ] **Cross-OS baseline:** where a host libm provides the same function,
      add it to the portable comparison harness so substrate results are diffed
      against the host. (REQ: REQ-24-0040)
- [ ] **Link-completeness test:** the §24.1 symbol-audit fixture passes (every
      added symbol resolves from `-lm`). (REQ: REQ-24-0041)

### 24.8 Documentation (Directive 9)

- [ ] **Man pages** under `usr.man/man3/` for each new family, following Linux
      `man-pages` style with `LIBRARY`, `SEE ALSO`, `ERRORS`, and
      `EXAMPLE`/`EXAMPLES` sections: `scalb.3`, `significand.3`, `drem.3`,
      `gamma.3`, `pow10.3`, `matherr.3`, `fadd.3` (covering the narrowing
      family), `totalorder.3`, `canonicalize.3`, `getpayload.3` (covering
      get/set payload), `lgamma_r.3` (extend for the `f`/`l` variants), and
      `clog10.3`. (REQ: REQ-24-0042)
- [ ] **Deprecation notes:** the obsolescent §24.2 pages SHALL document the
      standard replacement (`scalbn`, `remainder`, `lgamma`, `exp10`) and that
      `matherr` is a no-op compatibility shim. (REQ: REQ-24-0043)
- [ ] **`ERRORS` accuracy:** each page documents the `errno` / `fenv` behaviour
      actually implemented, separately from `RETURN VALUE`. (REQ: REQ-24-0044)

## User Stories

- **US-24-0001**: As a developer porting SVID/4.3BSD-era numerical code to
  Substrate, I want `scalb`, `significand`, `drem`, `gamma`, `pow10`, and a
  no-op `matherr` available from `-lm`, so that legacy sources link and run
  without rewrites.
- **US-24-0002**: As a numerical-software author targeting ISO C23, I want the
  narrowing arithmetic functions (`fadd`/`dadd`/.../`fsqrt`) with correct
  single-rounding, so that I can compute narrow results without the
  double-rounding error of compute-then-cast.
- **US-24-0003**: As an author of robust floating-point libraries, I want the
  IEEE 754-2019 `totalorder`/`totalordermag`/`canonicalize` predicates and the
  NaN-payload `getpayload`/`setpayload`/`setpayloadsig` functions, so that I can
  sort, canonicalize, and tag NaNs portably.
- **US-24-0004**: As a developer writing multi-threaded code, I want
  `lgammaf_r` and `lgammal_r`, so that I can compute log-gamma and its sign at
  `float`/`long double` precision without the data race on the global
  `signgam`.
- **US-24-0005**: As a developer doing complex-valued analysis, I want
  `clog10` and its `f`/`l` variants, so that base-10 complex logarithms are
  available alongside the existing `clog` family.
- **US-24-0006**: As a Substrate maintainer, I want a symbol-audit fixture, full
  edge/property/`fenv` test coverage, and man pages for every added function, so
  that libm completion is mechanically verifiable and documented.

## INCOSE/EARS Requirements

> EARS classifications: **Ubiquitous** (always-active), **Event-driven**
> (`WHEN <trigger>`), **State-driven** (`WHILE <state>`), **Optional**
> (`WHERE <feature present>`), **Unwanted** (`IF <condition>, … SHALL`).

### Build & surface

- **REQ-24-0001** (Ubiquitous): The Substrate build shall include a libm
  symbol-audit fixture that fails if any function enumerated by this tasklist is
  unresolved when linking against `-lm`.
- **REQ-24-0002** (Ubiquitous): The libm build shall compile every new
  translation unit in both the static (`.o`) and position-independent
  (`.pic.o`) passes.
- **REQ-24-0003** (Ubiquitous): The libm build shall archive every new object
  into `libm.a` and link it into `libm.so.0` with the `ELFOSABI_SUBSTRATE`
  OSABI byte preserved.
- **REQ-24-0004** (Optional): WHERE a function belongs to ISO C23, the system
  shall declare it in `<math.h>`/`<complex.h>` only WHEN `__STDC_VERSION__ >=
  202311L`; WHERE a function is an obsolescent or glibc extension, the system
  shall declare it only under the corresponding `_DEFAULT_SOURCE` /
  `_XOPEN_SOURCE` / `_GNU_SOURCE` feature-test macro.
- **REQ-24-0005** (Unwanted): IF adding the new symbols would introduce a
  duplicate or conflicting prototype for an already-defined libm symbol, the
  change SHALL be rejected at build time.

### Obsolescent aliases (§24.2)

- **REQ-24-0006** (Ubiquitous): The libm library shall provide `scalb(x,n)`
  and `scalbf(x,n)` returning `x · 2ⁿ`, consistent with the existing `scalbl`.
- **REQ-24-0007** (Event-driven): WHEN `significand`/`significandf`/
  `significandl` is called with a finite non-zero argument `x`, the function
  shall return the value of `x` scaled into the interval `[1, 2)`.
- **REQ-24-0008** (Ubiquitous): The libm library shall provide `drem` and
  `dremf` producing results identical to `remainder` and `remainderf`.
- **REQ-24-0009** (Ubiquitous): The libm library shall provide `gamma` and
  `gammaf` computing the natural logarithm of `|Γ(x)|` (equivalent to `lgamma`/
  `lgammaf`) and setting `signgam` to the sign of `Γ(x)`.
- **REQ-24-0010** (Ubiquitous): The libm library shall provide `pow10`,
  `pow10f`, and `pow10l` returning `10ˣ`, identical to `exp10`/`exp10f`/
  `exp10l`.
- **REQ-24-0011** (Ubiquitous): The libm library shall provide a default
  `matherr` returning 0, so that SVID-era objects referencing `matherr` link;
  the library shall continue to report errors via `errno` and the floating
  environment rather than invoking `matherr`.
- **REQ-24-0012** (Optional): WHERE the §24.2 obsolescent functions are
  declared, the headers shall mark them deprecated and name the standard
  replacement.

### Narrowing arithmetic (§24.3)

- **REQ-24-0013** (Ubiquitous): The libm library shall provide the C23
  narrowing operations `f{add,sub,mul,div,fma,sqrt}`, `f{…}l`,
  `d{add,sub,mul,div,fma,sqrt}`, and `d{…}l`.
- **REQ-24-0014** (Event-driven): WHEN a narrowing operation is called, the
  function shall compute the infinitely-precise result of the operation on the
  wider operands and round it to the narrower return type **exactly once**.
- **REQ-24-0015** (State-driven): WHILE a non-default rounding mode is in
  effect, each narrowing operation shall perform its single final rounding under
  that mode.
- **REQ-24-0016** (Event-driven): WHEN a narrowing operation produces an
  inexact, overflowing, underflowing, invalid, or divide-by-zero result, the
  function shall raise exactly the corresponding `fenv` exception(s).
- **REQ-24-0017** (Unwanted): IF an implementation would compute a narrowing
  result by rounding twice (operation then cast), that implementation SHALL be
  treated as non-conforming and replaced by a single-rounding implementation.

### Total order & NaN payload (§24.4)

- **REQ-24-0018** (Ubiquitous): The libm library shall provide
  `totalorder{,f,l}` and `totalordermag{,f,l}` implementing the IEEE 754-2019
  `totalOrder` predicate over all representable values, including distinct
  signed zeros and all NaN encodings.
- **REQ-24-0019** (Ubiquitous): The libm library shall provide
  `canonicalize{,f,l}`, `getpayload{,f,l}`, `setpayload{,f,l}`, and
  `setpayloadsig{,f,l}`.
- **REQ-24-0020** (Event-driven): WHEN `setpayload`/`setpayloadsig` is called
  with a valid payload, the function shall store a quiet (respectively
  signaling) NaN carrying that payload and return 0.
- **REQ-24-0021** (Unwanted): IF `setpayload`/`setpayloadsig` is called with a
  payload that does not fit the target type's significand, the function shall
  store `+0` and return nonzero.
- **REQ-24-0022** (Event-driven): WHEN `getpayload` is called with a non-NaN
  argument, the function shall return `-1`.
- **REQ-24-0023** (Ubiquitous): `totalorder*`, `totalordermag*`, and
  `getpayload*` shall not raise any floating-point exception, even for
  signaling-NaN inputs.

### Reentrant gamma & complex log (§24.5–24.6)

- **REQ-24-0024** (Ubiquitous): The libm library shall provide `lgammaf_r` and
  `lgammal_r` returning log-gamma and writing the sign of Γ to the caller's
  `int *`.
- **REQ-24-0025** (Unwanted): IF `lgammaf_r`/`lgammal_r` is called, the function
  SHALL NOT read or modify the global `signgam`.
- **REQ-24-0026** (Ubiquitous): The libm library shall provide `clog10`,
  `clog10f`, and `clog10l` computing the complex base-10 logarithm with the same
  branch cut as `clog`.

### Verification

- **REQ-24-0027** (Ubiquitous): For every added function, the test suite shall
  include reference-vector unit tests and special-value edge tests covering
  `±0`, `±Inf`, qNaN, sNaN, subnormals, and extremal finite magnitudes.
- **REQ-24-0028** (Ubiquitous): The test suite shall include property tests for
  narrowing single-rounding correctness, `totalOrder` totality/antisymmetry/
  transitivity, payload round-trip, and `clog10` identity.
- **REQ-24-0029** (Event-driven): WHEN a host libm provides an equivalent
  function, the portable test harness shall diff substrate's result against the
  host's at the same precision.
- **REQ-24-0030** (Ubiquitous): Each new function family shall ship a
  `usr.man/man3/` manual page conforming to Directive 9 (`LIBRARY`, `SEE ALSO`,
  `ERRORS` separate from `RETURN VALUE`, and `EXAMPLE`/`EXAMPLES`).
