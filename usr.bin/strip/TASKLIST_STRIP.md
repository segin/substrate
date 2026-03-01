# `usr.bin/strip` Tasklist

Goal: implement `strip` as a constrained `libelfobj` transform.

---

## 1. `libelfobj` Integration

- [ ] Open input with `elf_open()`.
- [ ] Create mutable output with `elf_create()` matching input class/endian/machine.
- [ ] Copy sections selectively based on strip mode.
- [ ] Copy program headers for ET_EXEC/ET_DYN.
- [ ] Finalize and write with `elf_write_file()`.
- [ ] Close both handles.
- [ ] In‑place mode: write to temp file, `rename()` over original.

## 2. Strip Modes

### 2a. `--strip-all` / `-s` (default)
- [ ] Remove `.symtab` and its associated `.strtab`.
- [ ] Remove all `SHT_SYMTAB` sections.
- [ ] Remove debug sections: `.debug_*`, `.zdebug_*`, `.line`, `.stab`, `.stabstr`, `.comment`.
- [ ] Remove `.note.GNU-stack` and other non‑essential notes.
- [ ] Preserve `.dynsym`, `.dynstr`, `.hash`, `.gnu.hash` — required for dynamic linking.
- [ ] Preserve all `SHF_ALLOC` sections.
- [ ] Preserve `.shstrtab` (section header string table).
- [ ] Preserve program headers unchanged.

### 2b. `--strip-debug` / `-g` / `-S`
- [ ] Remove only debug sections (`.debug_*`, `.zdebug_*`, `.line`, `.stab*`).
- [ ] Preserve `.symtab` and `.strtab` (useful for `nm`, `gprof`).
- [ ] Preserve `.comment` section.

### 2c. `--strip-unneeded`
- [ ] Remove symbols not referenced by any `SHT_REL` / `SHT_RELA` section.
- [ ] Build a set of referenced symbol indices from all relocation entries.
- [ ] Keep `STB_GLOBAL` and `STB_WEAK` defined symbols (needed for linking).
- [ ] Remove `STB_LOCAL` symbols not referenced by relocations.
- [ ] For ET_EXEC/ET_DYN: equivalent to `--strip-all` (relocations resolved at link time).

### 2d. `--strip-dwo`
- [ ] Remove `.dwo` / DWARF split debug sections only (for fission workflows).

## 3. Symbol Control Flags

- [ ] `--keep-symbol=<name>` / `-K <name>`: protect specific symbol from removal.
- [ ] `--keep-symbols=<file>`: read keep‑list from file.
- [ ] `--strip-symbol=<name>` / `-N <name>`: remove specific symbol (even in `--strip-debug` mode).
- [ ] `--keep-file-symbols`: retain `STT_FILE` symbols.
- [ ] `--discard-all` / `-x`: remove all non‑global symbols (equivalent to `--strip-unneeded` for locals).
- [ ] `--discard-locals` / `-X`: remove compiler‑generated local symbols (`.L*` labels).

## 4. Section Control Flags

- [ ] `--remove-section=<name>` / `-R <name>`: force removal of specific section.
- [ ] `--keep-section=<name>`: prevent section removal even in strip‑all mode.

## 5. Output Options

- [ ] `-o <file>` / `--output-file=<file>`: write to different file instead of in‑place.
- [ ] `-p` / `--preserve-dates`: preserve original atime/mtime.
- [ ] `-D` / `--enable-deterministic-archives`: deterministic mode (zero timestamps if stripping archive members).
- [ ] `-w` / `--wildcard`: allow wildcard patterns in symbol names.
- [ ] `-v` / `--verbose`: print each section/symbol removed.

## 6. In‑Place Safety

- [ ] Write to `<file>.XXXXXX` temporary file in same directory.
- [ ] Copy file permissions from original via `fstat()` + `fchmod()`.
- [ ] `rename()` temp over original on success.
- [ ] `unlink()` temp on failure — original is untouched.
- [ ] Handle `strip` on a running executable (rename‑based replacement is safe on Unix).

## 7. Section/Program Header Consistency

- [ ] After removing sections: renumber all `sh_link` / `sh_info` references.
- [ ] After removing `.symtab`: update any `SHT_REL`/`SHT_RELA` `sh_link` that pointed to it.
- [ ] Verify no `PT_LOAD` segment references a removed section.
- [ ] Recalculate `e_shnum`, `e_shstrndx` in ELF header.
- [ ] Preserve `PT_NOTE`, `PT_GNU_STACK`, `PT_GNU_RELRO` segments.
- [ ] Preserve `PT_DYNAMIC` segment and all dynamic section entries.
- [ ] Preserve `.gnu.hash` / `.hash` for dynamic linker symbol lookup.
- [ ] Preserve `.interp` section and `PT_INTERP` segment.

## 8. ET_REL Specifics

- [ ] Relocatable objects use `.symtab` for relocation; `--strip-all` should warn it may break linking.
- [ ] `--strip-debug` is the safe default for ET_REL.
- [ ] `--strip-unneeded` on ET_REL: analyze relocation references to determine keepable symbols.

## 9. Error Handling

- [ ] Non‑ELF input: `strip: <file>: file format not recognized`, exit 1.
- [ ] Read‑only file: report permission error, exit 1.
- [ ] Multiple files: process all, exit 1 if any failed.
- [ ] `--keep-symbol` with nonexistent symbol: no error (be permissive).
- [ ] Stripping an already‑stripped binary: no‑op, output identical to input.

## 10. Build System

- [ ] Create `Makefile` linking `libelfobj.a`.
- [ ] `NATIVE_BUILD=1` support.
- [ ] `install` to `$(DESTDIR)/usr/bin/strip`.

## 11. Testing

### 11a. Strip Mode Tests
- [ ] `--strip-all` on ET_EXEC: `.symtab` removed, `.dynsym` preserved, program runs.
- [ ] `--strip-all` on ET_DYN: `.dynsym` preserved, `dlopen()` works.
- [ ] `--strip-debug` on ET_REL: `.debug_*` removed, `.symtab` preserved, `ld` can still link.
- [ ] `--strip-unneeded` on ET_REL: unreferenced locals removed, referenced locals kept.
- [ ] `--strip-dwo`: only `.dwo` sections removed.

### 11b. Symbol Control Tests
- [ ] `-K foo --strip-all`: `foo` survives strip.
- [ ] `-N bar --strip-debug`: `bar` removed even though `.symtab` is kept.
- [ ] `-x`: all locals removed.
- [ ] `-X`: `.L*` labels removed, named locals kept.

### 11c. Section Control Tests
- [ ] `-R .comment`: `.comment` removed.
- [ ] `--keep-section=.note.ABI-tag --strip-all`: note preserved.

### 11d. Integrity Tests
- [ ] Stripped ET_EXEC runs correctly (test with a hello‑world program).
- [ ] Stripped ET_DYN can be dynamically loaded.
- [ ] Stripped ET_REL (`--strip-debug`) can be linked into a final binary.
- [ ] `readelf -a` reports no errors on stripped output.
- [ ] `nm` on `--strip-all` output reports "no symbols".

### 11e. In‑Place Safety Tests
- [ ] Simulate write failure: original file untouched.
- [ ] Strip read‑only file: error, original untouched.
- [ ] `--preserve-dates`: mtime unchanged after strip.

### 11f. Idempotency Tests
- [ ] Strip an already‑stripped binary: output identical to input.
- [ ] Double‑strip: no crash, no further size reduction.

### 11g. Edge Cases
- [ ] ELF with no `.symtab` (already stripped): `--strip-all` is no‑op.
- [ ] ELF with empty `.symtab` (0 entries): removed cleanly.
- [ ] ELF32 and ELF64 inputs.
- [ ] Very large binary (>100 sections): all processed.

### 11h. Integration Tests
- [ ] `cc -g -o prog prog.c` → `strip -g prog` → `gdb prog` reports "no debug info".
- [ ] `cc -g -o prog prog.c` → `strip prog` → prog still executes.
- [ ] Compare output against host `strip` on same binary.

## 12. Man Page

- [ ] Write `strip.1` covering all modes, flags, and exit codes.
- [ ] Document which sections are removed in each mode.
- [ ] Install to `$(DESTDIR)/usr/man/man1/`.
