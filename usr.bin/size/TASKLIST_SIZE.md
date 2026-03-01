# `usr.bin/size` Tasklist

Goal: implement `size` using section accounting from `libelfobj`.

---

## 1. `libelfobj` Integration

- [x] Open input files with `elf_open()`.
- [x] Iterate sections via `elf_section_count()` / `elf_section_get()`.
- [x] Classify sections using `elf_section_type()`, `elf_section_flags()`, `elf_section_size()`.
- [x] Use `elf_class()` for correct 32/64‑bit formatting.
- [x] Call `elf_close()` on all handles.
- [x] Handle `elf_open()` failure with clear diagnostic.

## 2. Section Classification

- [x] **Text**: `SHF_ALLOC | SHF_EXECINSTR` (any combination with exec).
- [x] **Data**: `SHF_ALLOC | SHF_WRITE` and `sh_type != SHT_NOBITS`.
- [x] **BSS**: `SHF_ALLOC | SHF_WRITE` and `sh_type == SHT_NOBITS`.
- [x] **Read‑only data**: `SHF_ALLOC` without `SHF_WRITE` or `SHF_EXECINSTR`, `sh_type != SHT_NOBITS` — classify as text (Berkeley) or data (SysV).
- [x] TLS sections (`SHF_TLS`): include in data/bss as appropriate.
- [x] Non‑`SHF_ALLOC` sections (debug, strtab, symtab): exclude from all totals.
- [x] Handle `SHT_NOTE` sections: include in text or data per flags.

## 3. Output Formats

### 3a. Berkeley Format (default / `-B`)
- [x] Header line: `   text    data     bss     dec     hex filename`.
- [x] One row per file: text, data, bss as decimal, total as decimal + hex.
- [x] Multiple files: one row each + grand total row.

### 3b. SysV Format (`-A` / `--format=sysv`)
- [x] Per‑section breakdown table: Section, Size, Addr.
- [x] Per‑file section table with header: `<filename>  :`.
- [x] Total row at bottom.

### 3c. GNU Format (`--format=gnu`)
- [x] Header: `      text       data        bss      total filename`.
- [x] Same as Berkeley but with wider columns.

### 3d. Radix Options
- [x] `-d` / `--radix=10`: decimal (default).
- [x] `-o` / `--radix=8`: octal.
- [x] `-x` / `--radix=16`: hexadecimal.
- [x] `--radix=10` applies to all numeric columns in all formats.

## 4. Multi‑File and Archive Support

- [x] Multiple files on command line: print each, then total.
- [x] Archive input (`.a`): iterate members, report per‑member on separate rows.
- [x] Print archive member as `<archive>(<member>)` in filename column.
- [x] Non‑ELF members in archive: skip with diagnostic, continue.
- [x] Thin archives: resolve paths, open from disk.

## 5. Totals and Overflow

- [x] Use `uint64_t` accumulators for all size sums.
- [x] Handle ELF64 sections with sizes > 4 GiB.
- [x] `--totals` / `-t`: print grand total across all files (default for multiple files).

## 6. Flags

- [ ] `-A` / `--format=sysv`: SysV format.
- [ ] `-B` / `--format=berkeley`: Berkeley format (default).
- [ ] `--format=gnu`: GNU format.
- [ ] `-d` / `-o` / `-x` / `--radix={8,10,16}`: number radix.
- [ ] `-t` / `--totals`: print grand total row.
- [ ] `--common`: include `SHN_COMMON` symbols in BSS total.
- [ ] `--target=<bfdname>`: ignored (compat with GNU size).
- [ ] `-V` / `--version`: version string.
- [ ] `-h` / `--help`: usage.

## 7. Error Handling

- [ ] Non‑ELF input: `size: <file>: file format not recognized`, exit 1.
- [ ] Permission denied: report, continue to next file.
- [ ] Set exit code 1 if any file fails.

## 8. Build System

- [ ] Create `Makefile` linking `libelfobj.a`.
- [ ] `NATIVE_BUILD=1` support.
- [ ] `install` to `$(DESTDIR)/usr/bin/size`.

## 9. Testing

### 9a. Classification Tests
- [ ] ET_REL with `.text`, `.data`, `.bss`, `.rodata` — verify correct buckets.
- [ ] ET_REL with TLS section — included in data or bss.
- [ ] ET_REL with no `SHF_ALLOC` sections — all zeros.

### 9b. Format Tests
- [ ] Berkeley output matches expected column layout.
- [ ] SysV output matches per‑section table layout.
- [ ] `-x` radix: all numbers in hex.
- [ ] `-o` radix: all numbers in octal.

### 9c. Multi‑File Tests
- [ ] Two files: two rows + total row.
- [ ] Archive with 3 `.o` members: 3 rows with `archive(member)` names.

### 9d. Overflow Tests
- [ ] Mock ELF64 object with `.bss` size > 4 GiB — total does not wrap.

### 9e. ELF Variant Tests
- [ ] ELF32 LE ET_REL.
- [ ] ELF64 LE ET_REL.
- [ ] ET_EXEC and ET_DYN — segment‑based vs section‑based accounting matches.
- [ ] Stripped ELF (no `.symtab`): sections still countable.

### 9f. Integration Tests
- [ ] `cc -c foo.c` → `size foo.o` — verify text ≥ compiled code size.
- [ ] Compare output against host `size` on same `.o`.

## 10. Man Page

- [ ] Write `size.1` covering all flags and format descriptions.
- [ ] Install to `$(DESTDIR)/usr/man/man1/`.
