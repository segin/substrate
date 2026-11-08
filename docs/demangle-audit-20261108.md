# Audit of `libdemangle` (`usr.lib/demangle`) — 2026-11-08

Comprehensive read-only audit of the C++/Rust/D symbol demangling library under
`usr.lib/demangle` (`buffer.c`, `demangle.c`, `itanium.c`, `rust.c`, `dlang.c`,
`demangle_internal.h`, `Makefile`), public header `include/demangle.h`, and
manual pages `usr.man/man3/demangle*.3`.

Every finding is written as an actionable remediation checklist item paired
with an INCOSE/EARS requirement (`REQ-DMGA-nnnn`) in the requirements index at
the end of this document, using the standard EARS patterns:

- **Ubiquitous**: `The <system> shall <response>.`
- **Event-driven**: `When <trigger>, the <system> shall <response>.`
- **State-driven**: `While <state>, the <system> shall <response>.`
- **Unwanted behaviour**: `If <condition>, then the <system> shall <response>.`
- **Optional feature**: `Where <feature>, the <system> shall <response>.`

**21 items: 6 high, 11 medium, 4 low.**

---

## A. Security, Memory Safety & Denial-of-Service

- [x] **DMG-RUST-01** (high) Rust v0 backreferences allow self- and forward-references in paths, types, and constants (REQ: REQ-DMGA-0001)
    - Site: `usr.lib/demangle/rust.c:1324-1339` (`rust_parse_v0_path`), `usr.lib/demangle/rust.c:860-888` (`rust_parse_v0_const`), `usr.lib/demangle/rust.c:1244-1274` (`rust_parse_v0_type_inner`)
    - Evidence: `rust_parse_v0_path` resolves `<backref> = "B" <base-62-number>` via `rust_parser_resolve_backref(p, off, &target)` and jumps to `p->cur = target` without verifying `target < start`. A self-referential or forward backref (e.g., `_RB_`) recurses until `recursion_limit` (128), or exhausts the call stack (`SIGSEGV`) when `DEMANGLE_NO_RECURSE_LIMIT` is set. In `rust_parse_v0_const` (line 871) and `rust_parse_v0_type_inner` (line 1256), the check is `target >= save_cur` (after the `'B' <base62>` token), which permits `target == backref_start` (pointing to the `'B'` token itself) and also causes infinite recursion.
    - Fix: Record `backref_start = p->cur` before consuming `'B'` in all three sites and reject any backref where `target >= backref_start`. Remove the unused `p->backrefs` array and `rust_parser_add_backref()` dead allocations.
    - Verify: `tests/usr.lib/demangle/`: `_RB_`, `_RIC0B_E`, and self-referential type/const backrefs return `NULL` both with and without `DEMANGLE_NO_RECURSE_LIMIT`.

- [x] **DMG-DLANG-01** (high) `dlang_take_segment()` invalidates recorded `dlang_name_ref_t` output slices (REQ: REQ-DMGA-0002)
    - Site: `usr.lib/demangle/dlang.c:239-301` (`dlang_parser_add_name_ref`, `dlang_parser_append_name_ref`) vs. `usr.lib/demangle/dlang.c:666-689` (`dlang_take_segment`)
    - Evidence: `dlang_name_ref_t` stores `{off, len}` offsets into `p->out.data`. `dlang_take_segment()` rewinds `p->out.len = off` and rewrites `p->out.data` (when wrapping type qualifiers, function return types, pointers, arrays, associative arrays, delegates, or template value arguments). Any `name_ref` recorded inside a taken segment points either past `p->out.len` or into overwritten output bytes.
    - Fix: Store owned copies (or immutable input-span pointers/lengths) in `p->name_refs` instead of mutable offsets into `p->out.data`.
    - Verify: `tests/usr.lib/demangle/`: a D symbol whose `Q` backreference refers to a symbol parsed inside a qualified type or function parameter list resolves to the original identifier text rather than overwritten qualifier/return-type text.

- [x] **DMG-ITAN-01** (high) `parse_expr_primary()` leaves dangling substitution offsets and dirty parser state on rewind/failure (REQ: REQ-DMGA-0003)
    - Site: `usr.lib/demangle/itanium.c:1450-1455` (`parse_expr_primary`)
    - Evidence: `parse_expr_primary()` saves `mark_save(p, &m)`, calls `parse_type(p)` to skip the literal's type, and then rewinds `p->out.len = m.out_len` without restoring `p->substitution_count` (or calling `mark_restore(p, &m)` when `parse_type(p)` fails). Substitutions registered inside `parse_type(p)` retain `.off >= m.out_len`, later resolving to unrelated characters appended to `p->out`.
    - Fix: Restore `mark_restore(p, &m)` on failure, and either suppress substitution registration while skipping the literal type or restore `p->substitution_count` when rewinding `p->out.len`.
    - Verify: `tests/usr.lib/demangle/`: a template literal argument with a compound/qualified type followed by `S_` / `S0_` resolves the substitution to the outer symbol component rather than bytes from the rewound literal type.

- [x] **DMG-ITAN-02** (high) `mark_save()` and `mark_restore()` do not preserve `has_return_type` or `last_template_*` entries (REQ: REQ-DMGA-0004)
    - Site: `usr.lib/demangle/itanium.c:71-78` (`dm_parse_mark_t`), `usr.lib/demangle/itanium.c:474-497` (`mark_save`, `mark_restore`)
    - Evidence: `dm_parse_mark_t` omits `p->has_return_type` and the elements of `p->last_template_off` / `p->last_template_len`. When speculative parses in `parse_template_arg()` or `parse_expression()` parse a template (`I...E`) and then fail and backtrack, `p->has_return_type` remains `1` and `p->last_template_*` remains overwritten.
    - Fix: Include `has_return_type` and the active slice of `last_template_off` / `last_template_len` in `dm_parse_mark_t`, and restore them in `mark_restore()`.
    - Verify: `tests/usr.lib/demangle/`: backtracking over a failed template type parse does not cause a non-template outer function to consume its first parameter as a return type.

- [x] **DMG-NUM-01** (high) Integer overflow and signed conversion defects in number parsers (REQ: REQ-DMGA-0005)
    - Site: `usr.lib/demangle/dlang.c:430-437` (`dlang_parse_template_value`), `usr.lib/demangle/rust.c:1584-1591` (`rust_parse_decimal_span`), `usr.lib/demangle/itanium.c:586-613` (`parse_signed_number_token`)
    - Evidence:
        1. `dlang_parse_template_value()` computes `n = n * 10u + (size_t)(p->cur[0] - '0')` with no overflow check, allowing wraparound on long digit runs.
        2. `rust_parse_decimal_span()` uses the post-multiply `nv < v` check, which misses overflows when `v` is in `[SIZE_MAX / 10, SIZE_MAX / 9)`.
        3. `parse_signed_number_token()` parses `size_t mag` up to `SIZE_MAX` and casts to `(long long)mag` / `-(long long)mag`, invoking implementation-defined signed overflow when `mag > (size_t)LLONG_MAX`.
    - Fix: Use pre-multiply saturation (`v > ((size_t)-1 - d) / 10u`) in `dlang_parse_template_value()` and `rust_parse_decimal_span()`, and bound `mag <= (size_t)LLONG_MAX` in `parse_signed_number_token()`.
    - Verify: `tests/usr.lib/demangle/`: inputs with 20+ digit numbers in D template values, Rust legacy length spans, and Itanium thunk offsets fail cleanly (`NULL`).

- [x] **DMG-UTF8-01** (high) UTF-8 encoder accepts UTF-16 surrogates and 64-bit legacy `$u...$` escapes truncate `unsigned long` to `uint32_t` (REQ: REQ-DMGA-0006)
    - Site: `usr.lib/demangle/rust.c:508-536` (`rust_utf8_append_codepoint`), `usr.lib/demangle/rust.c:1651-1675` (`rust_legacy_decode_token`)
    - Evidence:
        1. `rust_utf8_append_codepoint()` accepts `0xD800 <= cp <= 0xDFFF`, emitting ill-formed UTF-8 (CESU-8 surrogates).
        2. `rust_legacy_decode_token()` accumulates `unsigned long cp` bounded by `ULONG_MAX` and casts `(uint32_t)cp` at line 1672. On 64-bit targets (`x86_64`), `$u100000041$` wraps modulo $2^{32}$ to `0x41` (`'A'`) instead of being rejected.
    - Fix: Reject `cp >= 0xD800u && cp <= 0xDFFFu` in `rust_utf8_append_codepoint()`, and reject `cp > 0x10FFFFul` during accumulation in `rust_legacy_decode_token()`.
    - Verify: `tests/usr.lib/demangle/`: legacy symbols containing `$ud800$` or `$u100000041$` and Punycode identifiers decoding to surrogates return `NULL`.

---

## B. Itanium C++ ABI Compliance (`itanium.c`)

- [x] **DMG-ITAN-03** (medium) Function return types are inserted at buffer offset `0` instead of the current encoding start (REQ: REQ-DMGA-0007)
    - Site: `usr.lib/demangle/itanium.c:1204-1242` (`parse_bare_function_type`)
    - Evidence: `buf_insert(&p->out, 0, ...)` inserts the return type at offset `0` of `p->out`. When `parse_encoding()` is called recursively from `parse_local_name()` (`_ZZ...`) or `parse_special_name()` (thunks, transaction clones), the return type is inserted before the outer scope or `"non-virtual thunk to "` prefix (e.g., `int non-virtual thunk to foo<int>()`).
    - Fix: Track the start offset of the active `parse_encoding()` invocation in `dm_itanium_parser_t`, insert the return type at `encoding_off`, and shift only substitution/template offsets >= `encoding_off`.
    - Verify: `tests/usr.lib/demangle/`: `_ZThn8_N3Foo3barIiEEvT_` demangles to `"non-virtual thunk to void Foo::bar<int>(int) [offset -8]"`.

- [x] **DMG-ITAN-04** (medium) Pointer/reference-to-array formatting corrupts template arguments, ABI tags, and substitution offsets (REQ: REQ-DMGA-0008)
    - Site: `usr.lib/demangle/itanium.c:2002-2013` (`parse_type_inner`)
    - Evidence: To format pointers/references to arrays (`int (*)[4]`), `parse_type_inner()` scans forward from `before` for the first `'['` character and calls `buf_insert()`. If the pointee is a template with an array argument (`Foo<int[4]>*`) or an ABI tag (`Foo[abi:cxx11]*`), `'['` matches inside `<...>` or `[abi:...]`, producing `Foo<int (*)[4]>` or `Foo (*)[abi:cxx11]`. In addition, `buf_insert()` shifts `p->out` without adjusting substitution offsets >= `bracket`.
    - Fix: Only insert `" (*)"` / `" (&)"` / `" (&&)"` before `'['` when the direct pointee production is `A` (`parse_array_type`), and adjust any recorded substitution offsets >= `bracket`.
    - Verify: `tests/usr.lib/demangle/`: `_Z1fP3FooIA4_iE` demangles to `"f(Foo<int[4]>*)"` and `_Z1fP3FooB5cxx11` demangles to `"f(Foo[abi:cxx11]*)"`.

- [x] **DMG-ITAN-05** (medium) Multi-dimensional array bounds are emitted in reverse order (REQ: REQ-DMGA-0009)
    - Site: `usr.lib/demangle/itanium.c:1751-1786` (`parse_array_type`)
    - Evidence: `A2_A3_i` (`int[2][3]`) parses the element type `A3_i` (`int[3]`) first and appends `[2]` afterward, yielding `int[3][2]`.
    - Fix: Insert the outer dimension before any trailing array dimensions of the element type, or collect consecutive `A<dim>_` prefixes before formatting the element type.
    - Verify: `tests/usr.lib/demangle/`: `_Z1fA2_A3_i` demangles to `"f(int[2][3])"`.

- [x] **DMG-ITAN-06** (medium) Parameter pack expansion `Dp` fails on empty packs and non-zero pack indices (REQ: REQ-DMGA-0010)
    - Site: `usr.lib/demangle/itanium.c:2049-2091` (`parse_type_inner`)
    - Evidence: `Dp` unconditionally calls `parse_type(p)` once even when the pack has 0 elements (`J E`), causing empty pack instantiations to fail. For non-empty packs, it hardcodes `pack_start = 0` and overwrites `p->func_tmpl_off[0]`, failing when a function template has leading non-pack template parameters (`T_`, `T0_`) and leaking the overwritten `func_tmpl_off[0]` on error return at line 2082.
    - Fix: Track which template parameter index is a pack (and allow 0-length packs in `J...E`), omit the parameter when the pack is empty, and expand the referenced pack parameter index cleanly.
    - Verify: `tests/usr.lib/demangle/`: `_Z1fIJEEvDpT_` demangles to `"void f<>()"` and `_Z1fIiJdcEEvT_DpT0_` demangles to `"void f<int, double, char>(int, double, char)"`.

- [x] **DMG-ITAN-07** (medium) Standard library substitutions `Sa`, `Sb`, `Ss`, `Si`, `So`, `Sd` are skipped when registering nested template substitutions (REQ: REQ-DMGA-0011)
    - Site: `usr.lib/demangle/itanium.c:982-1026` (`parse_nested_name`)
    - Evidence: `is_wellknown_sub` treats `Sa`, `Sb`, `Ss`, `Si`, `So`, `Sd` identically to `St` and suppresses `parser_add_substitution()` both before and after `<template-args>`. Per the Itanium C++ ABI, only `St` (`::std::`) is a scope prefix that is not itself a substitution candidate; `Sa<...>` and `Sb<...>` are template instantiations that must be added to the substitution table.
    - Fix: Restrict the prefix-only exemption to `St`, or register substitutions for instantiated standard abbreviations per the Itanium ABI grammar.
    - Verify: `tests/usr.lib/demangle/`: a symbol referencing `S0_` after `NSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE...` or `NSaIcE...` resolves `S0_` to the expected substitution entry.

---

## C. Rust Symbol Demangling (`rust.c`)

- [x] **DMG-RUST-02** (medium) `DEMANGLE_RUST` rejects all legacy Rust symbols (REQ: REQ-DMGA-0012)
    - Site: `usr.lib/demangle/rust.c:1807-1829` (`demangle_rust`)
    - Evidence: When `(options & DEMANGLE_RUST) != 0`, `demangle_rust()` checks `if (!rust_is_v0(mangled)) return NULL;` and never calls `rust_demangle_legacy()`, contradicting `demangle(3)`.
    - Fix: Allow `demangle_rust()` to fall through to `rust_demangle_legacy()` when `rust_is_legacy(mangled)` is true under both `DEMANGLE_RUST` and `DEMANGLE_AUTO`.
    - Verify: `tests/usr.lib/demangle/`: `demangle("_ZN3foo3bar17h0123456789abcdefE", DEMANGLE_RUST)` returns `"foo::bar"`.

- [ ] **DMG-RUST-03** (medium) Rust v0 `M` and `X` impl paths reject optional `<impl-path>` disambiguators (REQ: REQ-DMGA-0013)
    - Site: `usr.lib/demangle/rust.c:1432-1475` (`rust_parse_v0_path`)
    - Evidence: In the Rust v0 grammar, `M` and `X` begin with `<impl-path> = [<disambiguator>] <path>`. `rust_parse_v0_path()` calls `rust_parse_v0_path(p)` directly without parsing the optional `s<base62>_` disambiguator first, failing on any inherent or trait impl with a disambiguated `<impl-path>` (e.g., `_RMs_...` or `_RXs_...`).
    - Fix: Call `rust_parse_optional_disambiguator(p, NULL, NULL)` before `rust_parse_v0_path(p)` in the `'M'` and `'X'` branches.
    - Verify: `tests/usr.lib/demangle/`: `_RNvMs0_Cs123_3foo3Bar3baz` demangles to `"<foo::Bar>::baz"`.

- [ ] **DMG-RUST-04** (medium) Rust v0 associated-type bindings in `dyn Trait` (`p`) are misparsed (REQ: REQ-DMGA-0014)
    - Site: `usr.lib/demangle/rust.c:981-997` (`rust_parse_v0_dyn_trait`)
    - Evidence: In the Rust v0 grammar, `<dyn-trait-assoc-binding> = "p" <undisambiguated-identifier> <type>`. `rust_parse_v0_dyn_trait()` sees `'p'`, outputs `" + Assoc="`, and calls `rust_parse_v0_generic_arg(p)` without consuming `'p'` or the identifier; `'p'` is then parsed as the placeholder type `_` and the identifier causes a parse failure.
    - Fix: Consume `'p'`, parse the associated type name via `rust_parse_v0_identifier(p)`, append `'='`, and parse the bound type via `rust_parse_v0_type(p)`.
    - Verify: `tests/usr.lib/demangle/`: `_RNDp4ItemlEL_...` formats the associated type binding as `Trait<Item = i32>`.

- [ ] **DMG-RUST-05** (medium) Rust v0 function pointers with non-`"C"` extern ABIs fail to parse (REQ: REQ-DMGA-0015)
    - Site: `usr.lib/demangle/rust.c:1143-1150` (`rust_parse_v0_type_inner`)
    - Evidence: In the Rust v0 grammar, `<abi> = "C" | <undisambiguated-identifier>`. After consuming `'K'`, the parser only checks `if (p->cur[0] == 'C')` and fails to consume `<undisambiguated-identifier>` for any other ABI (such as `K6system` for `extern "system"`).
    - Fix: When `p->cur[0] != 'C'`, parse the ABI name via `rust_parse_v0_identifier()` (converting `'_'` to `'-'` per the Rust v0 specification) and emit `extern "<abi>" `.
    - Verify: `tests/usr.lib/demangle/`: a function type with `FK6systemEu` demangles to `extern "system" fn()`.

---

## D. D Language Demangling (`dlang.c`)

- [x] **DMG-DLANG-02** (medium) D `Q` backreferences are parsed as decimal absolute indices instead of base-26 relative offsets (REQ: REQ-DMGA-0016)
    - Site: `usr.lib/demangle/dlang.c:609-617` (`dlang_parse_symbol_name`)
    - Evidence: `dlang_parse_symbol_name()` parses `'Q'` followed by `dlang_parse_number()` (decimal digits). Under the DMD ABI specification, `Q` backreferences encode a base-26 number (`'A'-'Z'` for upper digits, `'a'-'z'` for the terminal digit) representing a relative backward position in the identifier/type reference table.
    - Fix: Implement base-26 relative backreference decoding for `Q` per the D ABI specification (while optionally retaining decimal fallback only if needed by existing synthetic tests).
    - Verify: `tests/usr.lib/demangle/`: standard DMD/LDC/GDC symbols containing `Qa`, `Qb`, `QBa` backreferences demangle to their referenced identifiers.

- [ ] **DMG-DLANG-03** (medium) `demangle_dlang()` ignores `DEMANGLE_NO_PARAMS` and `DEMANGLE_NO_VERBOSE` (REQ: REQ-DMGA-0017)
    - Site: `usr.lib/demangle/dlang.c:1338-1410` (`dlang_parse_mangled_name`, `dlang_demangle_symbol`)
    - Evidence: `dlang_demangle_symbol()` casts `(void)options;`. Passing `DEMANGLE_NO_PARAMS` still appends `: <type>` and full function parameter lists; passing `DEMANGLE_NO_VERBOSE` still emits function attributes (`pure`, `nothrow`, `@safe`, `@nogc`, etc.).
    - Fix: When `DEMANGLE_NO_PARAMS` is set, omit the trailing `: <type>` signature in `dlang_parse_mangled_name()`; when `DEMANGLE_NO_VERBOSE` is set, suppress function attribute suffixes in `dlang_append_attrs()`.
    - Verify: `tests/usr.lib/demangle/`: `demangle("_D3foo3barFiZv", DEMANGLE_DLANG | DEMANGLE_NO_PARAMS)` returns `"foo.bar"`.

- [ ] **DMG-DLANG-04** (low) Unmangled `_d_...` runtime helper names are returned as demangled symbols (REQ: REQ-DMGA-0018)
    - Site: `usr.lib/demangle/dlang.c:114-118`, `usr.lib/demangle/dlang.c:1389-1391`, `usr.lib/demangle/dlang.c:1419-1421`, `usr.lib/demangle/demangle.c:82-84`
    - Evidence: `_d_...` symbols (such as `_d_allocclass`) are C-linkage runtime functions, not mangled D names, and `_d_` can collide with ordinary C identifiers starting with `_d_`. Returning `strdup(mangled)` reports a successful demangling where the output equals the input.
    - Fix: Confirm whether `tests/usr.lib/demangle` requires `_d_` passthrough; if retained, restrict it to known D runtime entrypoints or document the behavior in `demangle(3)`.
    - Verify: Per decision and regression test in `tests/usr.lib/demangle/`.

---

## E. Code Style, Duplication & Build Hygiene

- [ ] **DMG-MAINT-01** (low) Duplicated buffer wrapper functions across `itanium.c`, `rust.c`, and `dlang.c` (REQ: REQ-DMGA-0019)
    - Site: `usr.lib/demangle/itanium.c:146-230`, `usr.lib/demangle/rust.c:147-195`, `usr.lib/demangle/dlang.c:158-185`, `usr.lib/demangle/buffer.c`
    - Evidence: `itanium.c`, `rust.c`, and `dlang.c` each define their own `static` wrapper functions around `demangle_buf_*`. In addition, `buf_printf()` in `itanium.c:185-218` duplicates `demangle_buf_printf()` from `buffer.c:111-143`, and `buf_insert()` (`itanium.c:164-182`) is duplicated in `rust_buf_insert()` (`rust.c:164-183`).
    - Fix: Move `demangle_buf_insert()` into `buffer.c` / `demangle_internal.h` and call `demangle_buf_*` directly (or delegate `buf_printf` to `demangle_buf_printf`).
    - Verify: `make -C usr.lib/demangle clean all` and `make -C usr.lib/demangle NATIVE_BUILD=1 clean all` succeed with zero warnings.

- [ ] **DMG-MAINT-02** (low) `usr.lib/demangle/Makefile` `clean` target leaves `.d` dependency files and `dist/` artifacts (REQ: REQ-DMGA-0020)
    - Site: `usr.lib/demangle/Makefile:72-73`
    - Evidence: `make -C usr.lib/demangle clean` removes `$(OBJS)`, `$(PIC_OBJS)`, `$(O)$(LIBNAME)`, `$(O)$(SHLIB)`, and `$(MODE_STAMP)`, but leaves `*.d`, `*.pic.d`, and `usr.lib/demangle/dist/` in the working tree.
    - Fix: Remove `$(OBJS:.o=.d)` and `$(PIC_OBJS:.pic.o=.pic.d)` in `clean`, and remove untracked build artifacts from the source directory.
    - Verify: `make -C usr.lib/demangle clean` removes all generated `.o`, `.pic.o`, `.d`, and `.pic.d` files.

- [ ] **DMG-STYLE-01** (low) Source formatting deviates from `.agent/rules/code-style-guide.md` (REQ: REQ-DMGA-0021)
    - Site: `usr.lib/demangle/buffer.c`, `usr.lib/demangle/demangle.c`, `usr.lib/demangle/itanium.c`, `usr.lib/demangle/rust.c`, `usr.lib/demangle/dlang.c`
    - Evidence: The files use 4-space indentation instead of tabs, multi-line function headers (`static int\nfoo(...)`), spaces after control keywords (`if (...)`), and unparenthesized return statements (`return 0;` instead of `return(0);`), contrary to `.agent/rules/code-style-guide.md`.
    - Fix: Conform new and modified code in `usr.lib/demangle/` to `.agent/rules/code-style-guide.md`.
    - Verify: Code inspection against `.agent/rules/code-style-guide.md`.

---

## INCOSE / EARS Requirements Index

- **REQ-DMGA-0001** (EARS/Unwanted behaviour): If a Rust v0 backreference offset points to or past the start offset of the `B` backreference token itself, then `libdemangle` shall reject the symbol and return `NULL`.
  - Context: DMG-RUST-01, A Security, Memory Safety & Denial-of-Service
  - Verification: regression and fuzz test (`_RB_`, `_RIC0B_E`, with and without `DEMANGLE_NO_RECURSE_LIMIT`).

- **REQ-DMGA-0002** (EARS/Ubiquitous): The D demangler shall preserve all recorded symbol and type backreference strings across output-buffer segment extraction and rewriting.
  - Context: DMG-DLANG-01, A Security, Memory Safety & Denial-of-Service
  - Verification: regression test.

- **REQ-DMGA-0003** (EARS/Event-driven): When the Itanium demangler rewinds `p->out.len` or fails after parsing a literal type in `parse_expr_primary()`, the Itanium demangler shall restore the parser substitution table and mark state to match the rewound output buffer.
  - Context: DMG-ITAN-01, A Security, Memory Safety & Denial-of-Service
  - Verification: regression test.

- **REQ-DMGA-0004** (EARS/Event-driven): When the Itanium demangler backtracks via `mark_restore()`, the Itanium demangler shall restore `has_return_type` and the active `last_template_off` / `last_template_len` entries to their state at `mark_save()`.
  - Context: DMG-ITAN-02, A Security, Memory Safety & Denial-of-Service
  - Verification: regression test.

- **REQ-DMGA-0005** (EARS/Unwanted behaviour): If a numeric token in an Itanium, Rust, or D mangled name overflows `size_t` or `long long` bounds during parsing, then `libdemangle` shall reject the token before arithmetic wraparound occurs and return `NULL`.
  - Context: DMG-NUM-01, A Security, Memory Safety & Denial-of-Service
  - Verification: regression test.

- **REQ-DMGA-0006** (EARS/Unwanted behaviour): If a decoded Unicode codepoint in a Rust v0 Punycode identifier or Rust legacy `$u...$` escape falls outside `0x0000..0x10FFFF` or within the UTF-16 surrogate range `0xD800..0xDFFF`, then `libdemangle` shall reject the symbol and return `NULL`.
  - Context: DMG-UTF8-01, A Security, Memory Safety & Denial-of-Service
  - Verification: regression test on both 32-bit and 64-bit builds.

- **REQ-DMGA-0007** (EARS/Event-driven): When an Itanium function encoding includes a return type inside a local name or special-name wrapper, the Itanium demangler shall insert the return type at the start of that function encoding rather than at buffer offset `0`.
  - Context: DMG-ITAN-03, B Itanium C++ ABI Compliance
  - Verification: regression test.

- **REQ-DMGA-0008** (EARS/Event-driven): When formatting an Itanium pointer or reference type, the Itanium demangler shall insert `(*)`, `(&)`, or `(&&)` before `[` only when the direct pointee is an array type, and shall update any substitution offsets shifted by the insertion.
  - Context: DMG-ITAN-04, B Itanium C++ ABI Compliance
  - Verification: regression test.

- **REQ-DMGA-0009** (EARS/Event-driven): When formatting a multi-dimensional Itanium array type, the Itanium demangler shall emit array bounds in outer-to-inner declaration order.
  - Context: DMG-ITAN-05, B Itanium C++ ABI Compliance
  - Verification: regression test (`_Z1fA2_A3_i` -> `"f(int[2][3])"`).

- **REQ-DMGA-0010** (EARS/Event-driven): When expanding an Itanium parameter pack (`Dp`), the Itanium demangler shall expand 0-element packs to an empty parameter list and shall expand non-zero template parameter indices (`T0_`, `T1_`, ...) without corrupting `T_`.
  - Context: DMG-ITAN-06, B Itanium C++ ABI Compliance
  - Verification: regression test.

- **REQ-DMGA-0011** (EARS/Ubiquitous): The Itanium demangler shall register substitution table entries for standard library abbreviations (`Sa`, `Sb`, `Ss`, `Si`, `So`, `Sd`) and their template instantiations inside nested names in accordance with the Itanium C++ ABI.
  - Context: DMG-ITAN-07, B Itanium C++ ABI Compliance
  - Verification: regression test.

- **REQ-DMGA-0012** (EARS/Where-Optional feature): Where `DEMANGLE_RUST` is specified in `options`, `libdemangle` shall demangle both Rust v0 (`_R...`) and Rust legacy (`_ZN...17h<16-hex>E`) symbols.
  - Context: DMG-RUST-02, C Rust Symbol Demangling
  - Verification: regression test.

- **REQ-DMGA-0013** (EARS/Event-driven): When a Rust v0 `M` or `X` path production contains an `<impl-path>` disambiguator, the Rust demangler shall consume the optional disambiguator before parsing the `<impl-path>` path.
  - Context: DMG-RUST-03, C Rust Symbol Demangling
  - Verification: regression test.

- **REQ-DMGA-0014** (EARS/Event-driven): When a Rust v0 `dyn` trait bound includes an associated type binding (`p<ident><type>`), the Rust demangler shall parse both the identifier and the bound type.
  - Context: DMG-RUST-04, C Rust Symbol Demangling
  - Verification: regression test.

- **REQ-DMGA-0015** (EARS/Event-driven): When a Rust v0 function pointer specifies a non-`"C"` ABI identifier (`K<ident>`), the Rust demangler shall consume the identifier and format `extern "<abi>" `.
  - Context: DMG-RUST-05, C Rust Symbol Demangling
  - Verification: regression test.

- **REQ-DMGA-0016** (EARS/Event-driven): When a D mangled symbol contains a `Q` backreference, the D demangler shall decode the base-26 relative backreference offset in accordance with the D ABI specification.
  - Context: DMG-DLANG-02, D D Language Demangling
  - Verification: regression test.

- **REQ-DMGA-0017** (EARS/Optional feature): Where `DEMANGLE_NO_PARAMS` or `DEMANGLE_NO_VERBOSE` is set, the D demangler shall omit the trailing symbol type/parameter list or function attributes respectively.
  - Context: DMG-DLANG-03, D D Language Demangling
  - Verification: regression test.

- **REQ-DMGA-0018** (EARS/Ubiquitous): `libdemangle` shall either reject unmangled `_d_...` C-linkage symbols or document the `_d_` runtime helper passthrough contract in `demangle(3)`.
  - Context: DMG-DLANG-04, D D Language Demangling
  - Verification: per decision and manual page / regression test update.

- **REQ-DMGA-0019** (EARS/Ubiquitous): `usr.lib/demangle` shall provide shared buffer manipulation primitives (`demangle_buf_reserve`, `demangle_buf_append`, `demangle_buf_appendc`, `demangle_buf_insert`, `demangle_buf_printf`, `demangle_buf_take`, `demangle_buf_destroy`) in `buffer.c` without duplicating their implementations across parser translation units.
  - Context: DMG-MAINT-01, E Code Style, Duplication & Build Hygiene
  - Verification: build and test pass.

- **REQ-DMGA-0020** (EARS/Event-driven): When `make clean` is invoked in `usr.lib/demangle`, the build system shall remove all generated object, shared library, archive, stamp, and `.d` / `.pic.d` dependency files.
  - Context: DMG-MAINT-02, E Code Style, Duplication & Build Hygiene
  - Verification: `make -C usr.lib/demangle clean` inspection.

- **REQ-DMGA-0021** (EARS/Ubiquitous): C source files under `usr.lib/demangle` shall conform to `.agent/rules/code-style-guide.md`.
  - Context: DMG-STYLE-01, E Code Style, Duplication & Build Hygiene
  - Verification: code review.
