# `usr.bin/ar` Tasklist

Goal: implement a production `ar` where ELF member inspection/parsing comes from `libelfobj` only.

---

## 1. `libelfobj` Integration

- [x] Add `-lelfobj` to `Makefile` link flags and `-I` for `include/elfobj.h`.
- [x] Replace inline `Elf32_Ehdr`/`Elf32_Shdr`/`Elf32_Sym` parsing in `get_elf_symbols()` with `elf_open_memory()` + `elf_symbol_count()`/`elf_symbol_get()`.
- [x] Use `elf_symbol_bind()`, `elf_symbol_shndx()`, `elf_symbol_name()` instead of raw `ELF32_ST_BIND` / field access.
- [x] Remove hardcoded `#ifndef _ELF_H_SHDR` Shdr typedef and all local ELF constant guards (`STB_GLOBAL`, `STB_WEAK`, `SHN_UNDEF`, `ELFMAG`, `SELFMAG`).
- [x] Handle `elf_open_memory()` failures gracefully (non‑ELF members are legal in archives; skip symbol extraction, do not abort).
- [x] Call `elf_close()` after symbol extraction to avoid leaking `elfobj_t` handles.

## 2. ELF32/ELF64 and Cross‑Endian Support

- [x] Through `libelfobj`, transparently handle ELF64 members (`ELFCLASS64`) for symbol extraction.
- [x] Through `libelfobj`, transparently handle big‑endian ELF members (e.g. MIPS `.o` cross‑compiled on an LE host).
- [x] Validate: mixed ELF32 + ELF64 members in a single archive produce a correct `__.SYMDEF`.

## 3. Core Operations (`r`, `d`, `x`, `t`, `q`, `m`, `p`)

### 3a. Create / Replace (`r`)
- [x] `r` without members should be a no‑op write (not an error).
- [x] `rc` must suppress "creating archive" warning.
- [x] `ru` (update) must compare mtime correctly with `time_t`, not `atol` truncation.
- [x] On replace, preserve original member ordering (current code appends to tail on first‑insert only — verify).

### 3b. Quick Append (`q`)
- [x] `q` must not search for duplicates — always append.
- [x] `q` should refuse modifier `u` (update makes no sense with quick‑append).

### 3c. Delete (`d`)
- [x] `d` of a non‑existent member should warn but exit 0 (POSIX).
- [x] Deleting the only member must still write a valid (header‑only) archive.

### 3d. Extract (`x`)
- [x] Restore file mode from `ar_mode` header field (currently hardcoded `0644`).
- [x] Restore mtime via `utimes()` / `utimensat()` when `-o` (preserve timestamps) is given.
- [x] Restore uid/gid when running as root and not `-o` is specified (or new `--no-same-owner`).
- [x] Handle extraction of members whose names contain path components — refuse or strip depending on policy.

### 3e. List (`t`)
- [x] `tv` should print the timestamp in a human‑readable format (currently prints literal `"date"`).
- [x] `tv` should print file type character (`-` for regular) before permissions.

### 3f. Move (`m`)
- [x] Support positional modifiers `a`, `b`/`i` (after/before a named member).
- [x] Error if the anchor member does not exist.

### 3g. Print (`p`)
- [x] `p` must write raw member data to stdout without any header (current code is correct, verify no trailing newline is added).

## 4. Modifiers and Flags

- [x] Implement `a` (after), `b`/`i` (before) positional modifiers for `r` and `m`.
- [x] Implement `N count` (instance count) modifier for `d`, `x`, `t` when duplicate member names exist.
- [x] Implement `l` (use local directory for temp files — may be a no‑op on modern systems).
- [x] Implement `o` (preserve original timestamps on extract).
- [x] Implement `U` (deterministic mode: zero uid/gid/mtime, mode 0644) — critical for reproducible builds.
- [x] Implement `D` (alias for deterministic mode, matching GNU ar).
- [x] Implement `S` (do not generate symbol table even when writing).
- [x] Implement `T` (thin archive — store pathnames instead of member data).
- [x] Add `--plugin` flag silently accepted for GCC LTO compatibility (can be a no‑op).

## 5. Archive Format Variants

### 5a. BSD Format (current)
- [x] Audit BSD `#1/length` extended names — handle trailing NUL padding in name region.
- [x] Ensure `ar_size` field correctly accounts for in‑band name length.
- [x] Verify names > 155 characters work (currently `name_len` is unbounded, which is correct).

### 5b. GNU/SVR4 Format
- [x] Implement GNU long filename table member (`//`) containing concatenated `name/\n` entries.
- [x] Implement member name references as `/offset` into the `//` table.
- [x] Implement GNU‑style symbol table (`/`) with big‑endian 32‑bit offset array + string pool.
- [x] Implement 64‑bit GNU symbol table (`/SYM64/`) for archives > 4 GiB.
- [x] On read: detect format by first special member name and select parser accordingly.
- [x] On write: add `--format={bsd,gnu}` flag (default to GNU for toolchain compat with `ld`).

### 5c. Thin Archives
- [x] Implement thin archive magic (`!<thin>\n`).
- [x] Store relative paths instead of file content.
- [x] On extract/print: read file content from the referenced path at access time.
- [x] Thin archives must not embed a symbol table (symbols come from the on‑disk objects).

## 6. Symbol Table (`ranlib` / `s` modifier)

- [ ] Replace inline ELF parsing in `ranlib()` with `libelfobj` calls.
- [ ] Use `elf_open_memory_with_options(ELFOBJ_OPEN_NOCOPY)` for zero‑copy symbol scan.
- [ ] Generate sorted BSD `__.SYMDEF SORTED` by default (current code uses `__.SYMDEF`).
- [ ] Correctly compute member offsets in `write_archive()` — current two‑pass approach leaks sym_entry lists and misses extended‑name size adjustments.
- [ ] Support GNU `/` symbol table format when writing GNU archives.
- [ ] Handle archives with no ELF members (e.g. text files) — write archive without symbol table, no error.
- [ ] Handle `WEAK` vs `GLOBAL` binding: include both in symbol table (current behavior is correct, verify).
- [ ] Handle `COMMON` symbols (`SHN_COMMON`): include in symbol table with offset.
- [ ] Skip `STT_FILE` and `STT_SECTION` symbols (currently filtered by bind, verify no edge cases).
- [ ] `ranlib -t` (touch): update symbol table timestamp without rebuilding.
- [ ] Detect stale symbol table on read and warn (`ar` should still proceed).

## 7. Error Handling and Diagnostics

- [ ] Validate `ARMAG` on read; print clear error for non‑archive files (current: `errx` — good).
- [ ] Validate `ar_fmag` per member; recover or skip corrupt members instead of stopping (current: `warnx` + `break` — should `continue` to try remaining members).
- [ ] Validate `ar_size` field: reject negative/non‑numeric values, detect truncated archives.
- [ ] Warn on odd‑sized `ar_uid`/`ar_gid`/`ar_mode` fields (non‑numeric content).
- [ ] Detect and report truncated member data (short `read()` in `read_archive()`).
- [ ] Add `--verbose` diagnostic for format detection (`BSD` vs `GNU`).
- [ ] Check all `malloc()` / `strdup()` / `calloc()` return values — current code does not.
- [ ] Check all `read()` / `write()` / `fwrite()` return values — current code ignores short reads.
- [ ] Set correct exit codes: 0 success, 1 operational error, 2 usage error.

## 8. Deterministic / Reproducible Builds

- [ ] Implement `U`/`D` flag: zero uid, gid, mtime; force mode to `100644`.
- [ ] Implement `ZERO_AR_DATE` environment variable override (Debian convention).
- [ ] Implement `SOURCE_DATE_EPOCH` support: use as mtime when set.
- [ ] Ensure symbol table ordering is deterministic (sort by name, then by member position for ties).
- [ ] Ensure member ordering is stable across runs (insertion order, not filesystem order).

## 9. Memory Management and Robustness

- [ ] Free all `ar_memb` nodes and their `data`/`name` allocations before exit.
- [ ] Free sym_entry lists in `ranlib()` properly (current code leaks on some paths).
- [ ] Guard against integer overflow in `ar_size` (currently `atol` — use `strtol` with range check).
- [ ] Guard against archive members > 2 GiB (use `off_t` / `size_t` consistently).
- [ ] Guard against pathological archives with millions of members (linked‑list traversal is O(n²)).
- [ ] Use `fstat()` to validate file size before trusting header `ar_size` values.

## 10. POSIX and Portability Compliance

- [ ] Audit all operations against POSIX.1‑2017 `ar` specification.
- [ ] Accept key characters with or without leading `-` (current code strips `-` — good).
- [ ] Accept key characters in any order (current `while (*key)` loop — good).
- [ ] Reject unknown option letters with diagnostic (current: `warnx` + `usage()` — good).
- [ ] Implement POSIX `-C` (do not overwrite existing files on extract).
- [ ] Handle signals: flush partial writes on `SIGINT` to avoid corrupt archives.

## 11. Build System

- [ ] Link against `libelfobj.a` (`-L../../usr.lib/elfobj -lelfobj`).
- [ ] Add `elfobj.h` include path (`-I../../include`).
- [ ] Remove `-I$(TOP)/sys/exec/formats` once raw `elf.h` dependency is eliminated.
- [ ] Ensure `ranlib` symlink is created in `install` target (current: yes).
- [ ] Add `NATIVE_BUILD=1` support for host testing (link host libc + host `libelfobj`).
- [ ] Add `depend` target for header dependency tracking.

## 12. Testing

### 12a. Unit Tests
- [ ] Parse known‑good BSD archive and verify member list, sizes, names.
- [ ] Parse known‑good GNU archive and verify `//` long name table resolution.
- [ ] Parse archive with `__.SYMDEF SORTED` and verify symbol→member mapping.
- [ ] Parse archive with GNU `/` symbol table and verify symbol→member mapping.
- [ ] Round‑trip: create archive, read it back, verify byte‑identical member data.
- [ ] Verify `#1/length` extended name encoding/decoding for names with spaces and names > 15 chars.
- [ ] Verify `/offset` GNU long‑name encoding/decoding.

### 12b. ELF Symbol Extraction Tests
- [ ] ELF32 LE object: extract `STB_GLOBAL` + `STB_WEAK`, skip `STB_LOCAL` and `SHN_UNDEF`.
- [ ] ELF64 LE object: same as above.
- [ ] ELF32 BE object: verify endian swap via `libelfobj`.
- [ ] Non‑ELF member (plain text file): no crash, no symbols extracted.
- [ ] Stripped object (no `.symtab`): no symbols extracted, no error.
- [ ] Object with only `SHN_UNDEF` symbols: no symbols in table.
- [ ] Object with `STT_COMMON` symbols: included in symbol table.

### 12c. Operation Tests
- [ ] `ar r`: create new archive, add member, replace member, add second member.
- [ ] `ar ru`: skip replacement when member is older.
- [ ] `ar d`: delete single member, delete non‑existent member (warn, exit 0).
- [ ] `ar d`: delete only member → valid empty archive.
- [ ] `ar x`: extract all, extract named subset, verify file contents and permissions.
- [ ] `ar t`: list all members, list with verbose, verify format.
- [ ] `ar q`: quick‑append creates duplicates.
- [ ] `ar m`: move member to end, move with `a`/`b` positional modifier.
- [ ] `ar s`: generate symbol table, verify sorted order.
- [ ] `ranlib`: invocation via symlink triggers `ar -s` behavior.

### 12d. Edge Case Tests
- [ ] Duplicate member names: `ar r` replaces first occurrence.
- [ ] Duplicate member names with `N count`: delete Nth occurrence.
- [ ] Very long filenames (256+ chars) in both BSD and GNU format.
- [ ] Empty archive (just `!<arch>\n`): `ar t` produces no output, `ar x` is a no‑op.
- [ ] Archive with only a symbol table member: `ar t` produces no output.
- [ ] Member with zero‑length data.
- [ ] Member with odd‑length data (verify padding byte).
- [ ] Member name containing spaces (BSD `#1/` required).

### 12e. Integration Tests
- [ ] Build `.o` files with `cc -c`, archive with `ar rcs`, link with `ld` — end‑to‑end.
- [ ] Build `.o` files with `as`, archive with `ar rcs`, link with `ld` — end‑to‑end.
- [ ] Verify archives produced by Substrate `ar` are consumable by host `ld`.
- [ ] Verify archives produced by host `ar` are consumable by Substrate `ar t`/`ar x`.
- [ ] Verify `ranlib` output satisfies `ld` symbol resolution for a multi‑object library.
- [ ] Deterministic mode: two identical builds produce byte‑identical archives.

### 12f. Stress / Fuzz Tests
- [ ] Fuzz archive reader with AFL/libFuzzer on malformed headers.
- [ ] Fuzz archive reader with truncated files.
- [ ] Fuzz archive reader with corrupt `ar_size` / `ar_fmag` fields.
- [ ] Stress test: archive with 10,000 members — verify O(n) read performance.

## 13. Man Page

- [ ] Write `ar.1` man page covering all operations, modifiers, and exit codes.
- [ ] Write `ranlib.1` man page (or reference `ar.1` via `.so` redirect).
- [ ] Document BSD vs GNU format differences and `--format` flag.
- [ ] Document deterministic mode (`-U`/`-D`, `SOURCE_DATE_EPOCH`).
- [ ] Install man pages in `$(DESTDIR)/usr/share/man/man1/`.
