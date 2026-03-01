# `usr.bin/ranlib` Tasklist

Goal: implement standalone `ranlib` index generation using `libelfobj` for ELF symbol extraction.

> **Note:** `usr.bin/ar` already implements `ranlib` functionality when invoked as `ranlib`
> or with the `-s` flag. This standalone `ranlib` should either:
> (a) be a thin wrapper that execs `ar -s`, or
> (b) share the same archive I/O code with `ar` via a shared library/object.
>
> Decision: document which approach to take before implementation.

---

## 1. Architecture Decision

- [ ] Decide: standalone implementation vs. `exec ar -s "$@"` wrapper.
- [ ] If standalone: factor archive read/write code from `ar.c` into a shared `libar.a` or shared `.o`.
- [ ] If wrapper: implement as a trivial C program or shell script.

## 2. `libelfobj` Integration (if standalone)

- [ ] Open each archive member via `elf_open_memory()` / `elf_open_memory_with_options(ELFOBJ_OPEN_NOCOPY)`.
- [ ] Iterate symbols with `elf_symbol_count()` / `elf_symbol_get()`.
- [ ] Collect defined `STB_GLOBAL` + `STB_WEAK` symbols (skip `SHN_UNDEF`).
- [ ] Use `elf_symbol_name()`, `elf_symbol_bind()`, `elf_symbol_shndx()`.
- [ ] Handle `elf_open_memory()` failure for non‑ELF members (skip, no error).
- [ ] `elf_close()` every handle.

## 3. Archive I/O

### 3a. Reading
- [ ] Parse `!<arch>\n` magic.
- [ ] Parse BSD `#1/length` extended names.
- [ ] Parse GNU `//` long name table and `/offset` references.
- [ ] Skip existing symbol table members (`__.SYMDEF`, `__.SYMDEF SORTED`, `/`).
- [ ] Validate `ar_fmag` per member; warn and skip corrupt members.
- [ ] Record file offset of each member header for symbol→offset mapping.

### 3b. Writing
- [ ] Write `!<arch>\n` magic.
- [ ] Write symbol table as first member:
  - [ ] BSD format: `__.SYMDEF SORTED` with `ranlib` struct array + string pool.
  - [ ] GNU format: `/` with big‑endian uint32 count + offset array + string pool.
- [ ] Rewrite all subsequent members verbatim (preserve original data byte‑for‑byte).
- [ ] Ensure even alignment padding after each member.

### 3c. Atomicity
- [ ] Write to a temporary file alongside the archive.
- [ ] `rename()` temp over original on success.
- [ ] `unlink()` temp on failure.
- [ ] Preserve original file permissions via `fstat()` + `fchmod()`.

## 4. Symbol Table Details

- [ ] Sort symbols alphabetically by name (for `__.SYMDEF SORTED` / consistent `/`).
- [ ] Compute correct file offsets: offset of each member's `ar_hdr` from start of file.
- [ ] Handle archives > 4 GiB: emit `SYM64` table if any offset ≥ 2³².
- [ ] Handle members with no defined symbols: present in archive, absent from table.
- [ ] Handle `STT_COMMON` / `SHN_COMMON`: include in symbol table.
- [ ] Handle ELF32 + ELF64 mixed archives transparently via `libelfobj`.
- [ ] Deduplicate symbol names across members? No — include all (linker resolves first‑match).

## 5. Flags and Options

- [ ] Default (no flags): rebuild symbol table in‑place.
- [ ] `-t`: touch — update symbol table timestamp without full rebuild (if table already exists).
- [ ] `-s`: silent — suppress "no symbol table" diagnostics.
- [ ] `-D`: deterministic mode — zero uid/gid/mtime on symbol table member.
- [ ] `-U`: same as `-D` (GNU compat).
- [ ] `-v`: print each member name as it is processed.

## 6. Deterministic Mode

- [ ] When `-D`/`-U`: symbol table member gets uid=0, gid=0, mtime=0, mode=0100644.
- [ ] Honor `SOURCE_DATE_EPOCH` environment variable as mtime if set.
- [ ] Ensure symbol ordering is purely alphabetical (no insertion‑order dependency).

## 7. Error Handling

- [ ] Non‑archive input: `ranlib: <file>: file format not recognized`, exit 1.
- [ ] Archive with no ELF members: write archive with no symbol table, exit 0.
- [ ] Read‑only archive: `ranlib: <file>: permission denied`, exit 1.
- [ ] Truncated archive: warn, write what can be recovered.
- [ ] Check all `malloc()` / `read()` / `write()` returns.

## 8. Build System

- [ ] Create `Makefile` linking `libelfobj.a`.
- [ ] If wrapper approach: link or reference `ar` binary.
- [ ] `NATIVE_BUILD=1` for host testing.
- [ ] `install` to `$(DESTDIR)/usr/bin/ranlib`.

## 9. Testing

### 9a. Basic Tests
- [ ] Create archive without index → `ranlib` → verify `__.SYMDEF SORTED` or `/` present.
- [ ] Create archive with stale index → `ranlib` → verify updated offsets.
- [ ] `ranlib` on already‑indexed archive: idempotent (byte‑identical up to timestamp).

### 9b. Symbol Extraction Tests
- [ ] ELF32 `.o` with globals + locals + weak: only globals + weak defined in table.
- [ ] ELF64 `.o`: same behavior.
- [ ] Non‑ELF member: skipped, no symbols from it.
- [ ] Stripped `.o` (no `.symtab`): no symbols, no error.
- [ ] `SHN_COMMON` symbol: included in table.

### 9c. Multi‑Member Tests
- [ ] Archive with 5 `.o` files: all members have correct offsets in table.
- [ ] Archive with duplicate symbol across members: both entries in table (linker picks first).
- [ ] Archive with member having zero defined symbols: member absent from table.

### 9d. Format Tests
- [ ] BSD `__.SYMDEF SORTED`: verify `ranlib` struct layout and sorted string pool.
- [ ] GNU `/`: verify big‑endian uint32 count, offset array, and string pool.
- [ ] Round‑trip: `ar rcs` → `ranlib` (reindex) → `ld` link succeeds.

### 9e. Atomicity Tests
- [ ] Simulate write failure (full disk via `ulimit -f`): original archive untouched.
- [ ] Verify temp file cleanup on failure.

### 9f. Deterministic Tests
- [ ] `-D` mode: verify uid/gid/mtime of symbol table member are zero.
- [ ] Two identical invocations produce byte‑identical archives.

### 9g. Integration Tests
- [ ] `cc -c` → `ar r` (no `-s`) → `ranlib` → `ld` → executable runs.
- [ ] Verify archive produced by Substrate `ranlib` is consumable by host `ld`.

## 10. Man Page

- [ ] Write `ranlib.1` covering all flags, format details, and exit codes.
- [ ] Reference `ar(1)` for archive format details.
- [ ] Install to `$(DESTDIR)/usr/man/man1/`.
