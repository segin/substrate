# `usr.bin/nm` Tasklist

Goal: implement `nm` with ELF parsing exclusively via `libelfobj`.

---

## 1. `libelfobj` Integration

- [ ] Open input files with `elf_open()` / `elf_open_with_options()`.
- [ ] Read `.symtab` via `elf_symbol_count()` / `elf_symbol_get()`.
- [ ] Read `.dynsym` via dynamic symbol iteration APIs.
- [ ] Use `elf_symbol_name()`, `elf_symbol_bind()`, `elf_symbol_type()`, `elf_symbol_shndx()`, `elf_symbol_value()`, `elf_symbol_size()`.
- [ ] Use `elf_symbol_visibility()` for `STV_DEFAULT`/`HIDDEN`/`PROTECTED`/`INTERNAL` classification.
- [ ] Use `elf_symbol_version()` for versioned symbol display.
- [ ] Use `elf_section_name()` + `elf_section_type()` + `elf_section_flags()` for symbol‑type classification.
- [ ] Call `elf_close()` on every opened handle; no leaks on error paths.
- [ ] Handle `elf_open()` failure gracefully — print diagnostic, continue to next file.

## 2. Symbol Type Classification

- [ ] Map ELF section properties to BSD‑style type letters:
  - `T`/`t` — text (`.text`, `SHF_ALLOC|SHF_EXECINSTR`).
  - `D`/`d` — initialized data (`.data`, `SHF_ALLOC|SHF_WRITE`).
  - `B`/`b` — BSS (`.bss`, `SHT_NOBITS`, `SHF_ALLOC|SHF_WRITE`).
  - `R`/`r` — read‑only data (`.rodata`, `SHF_ALLOC` without `SHF_WRITE`/`SHF_EXECINSTR`).
  - `A` — absolute (`SHN_ABS`).
  - `C` — common (`SHN_COMMON`).
  - `U` — undefined (`SHN_UNDEF`).
  - `W`/`w` — weak symbol (any section).
  - `V`/`v` — weak object symbol.
  - `N` — debug symbol.
  - `n` — read‑only data in non‑alloc section.
  - `?` — unknown / unclassifiable.
- [ ] Uppercase for `STB_GLOBAL`/`STB_WEAK` defined, lowercase for `STB_LOCAL`.
- [ ] Weak undefined symbols: `w` (not `U`).

## 3. Output Modes and Flags

### 3a. Sort Options
- [ ] Default: alphabetical sort by symbol name.
- [ ] `-n` / `--numeric-sort`: sort by value (address), ties broken by name.
- [ ] `-p` / `--no-sort`: print in symbol table order.
- [ ] `-r` / `--reverse-sort`: reverse the current sort.
- [ ] `--size-sort`: sort by symbol size, largest first.

### 3b. Filter Options
- [ ] `-u` / `--undefined-only`: display only undefined symbols.
- [ ] `-g` / `--extern-only`: display only external (global + weak) symbols.
- [ ] `-a` / `--debug-syms`: include debug / compiler‑generated symbols (e.g. `.L` labels).
- [ ] `--defined-only`: display only defined symbols.
- [ ] `--no-weak`: exclude weak symbols.

### 3c. Display Options
- [ ] `-A` / `-o` / `--print-file-name`: prepend filename to each line.
- [ ] `-B`: BSD output format (default).
- [ ] `-P` / `--portability`: POSIX format: `name type value size`.
- [ ] `--format={bsd,sysv,posix}`: select output format explicitly.
- [ ] `-S` / `--print-size`: print symbol size alongside value.
- [ ] `-s` / `--print-armap`: print archive symbol index before members.
- [ ] `-l` / `--line-numbers`: use DWARF info to print source file and line (defer — requires `libelfobj` DWARF support).
- [ ] `-C` / `--demangle`: demangle C++ symbol names (defer unless tree needs it).
- [ ] `-D` / `--dynamic`: use `.dynsym` instead of `.symtab`.
- [ ] `-t {d,o,x}` / `--radix={d,o,x}`: print values in decimal, octal, or hex.
- [ ] `--special-syms`: include target‑specific special symbols.

### 3d. Multi‑File / Archive Support
- [ ] Multiple files on command line: print `\n<filename>:\n` banner between each.
- [ ] Archive files: iterate members, print banner per member, parse each via `libelfobj`.
- [ ] Thin archives: resolve member paths, open from disk.
- [ ] Non‑ELF files in an archive: skip with diagnostic, continue.

## 4. SysV Output Format

- [ ] Column headers: `Name`, `Value`, `Class`, `Type`, `Size`, `Line`, `Section`.
- [ ] Print `*UND*` for undefined symbols.
- [ ] Print section name for defined symbols.
- [ ] Fixed‑width columns aligned for readability.

## 5. Error Handling

- [ ] Non‑ELF input: `nm: <file>: file format not recognized` on stderr, exit 1.
- [ ] No symbol table in ELF: `nm: <file>: no symbols` on stderr, exit 0.
- [ ] Permission denied: report and continue to next file.
- [ ] Truncated ELF: report via `elf_last_diagnostics()` and skip.
- [ ] Set exit code 1 if any file could not be processed.

## 6. Build System

- [ ] Create `Makefile` linking against `libelfobj.a`.
- [ ] Include path for `elfobj.h` (`-I../../include`).
- [ ] `NATIVE_BUILD=1` support for host testing.
- [ ] `install` target to `$(DESTDIR)/usr/bin/nm`.

## 7. Testing

### 7a. Symbol Classification Tests
- [ ] ET_REL with `.text`, `.data`, `.bss`, `.rodata` symbols — verify type letters.
- [ ] Global, local, weak, weak‑undefined symbols — verify case and letter.
- [ ] `SHN_ABS` and `SHN_COMMON` symbols — verify `A` and `C`.
- [ ] `STT_FILE`, `STT_SECTION` symbols — hidden by default, shown with `-a`.

### 7b. Output Format Tests
- [ ] BSD format: `value type name`.
- [ ] POSIX format: `name type value size`.
- [ ] SysV format: column headers + aligned fields.
- [ ] Verify `-t d`, `-t o`, `-t x` radix switching.

### 7c. Sort and Filter Tests
- [ ] `-n`: verify sorted by value.
- [ ] `-p`: verify original table order.
- [ ] `-r`: verify reversed.
- [ ] `-u`: only undefined.
- [ ] `-g`: only global+weak.
- [ ] `-a`: includes debug symbols.
- [ ] `--defined-only`: excludes undefined.

### 7d. Archive Tests
- [ ] Archive with 3 `.o` files — verify per‑member banners.
- [ ] `-s` flag — print archive index.
- [ ] Archive with non‑ELF member — skip with diagnostic.
- [ ] Empty archive — no output.

### 7e. ELF Variant Tests
- [ ] ELF32 LE ET_REL.
- [ ] ELF64 LE ET_REL.
- [ ] ET_EXEC — both `.symtab` and `.dynsym`.
- [ ] ET_DYN — `.dynsym` only (stripped), `-D` flag.
- [ ] Stripped binary (no `.symtab`): "no symbols" message.

### 7f. Edge Cases
- [ ] Empty `.symtab` section.
- [ ] Symbol with value 0 — do not omit.
- [ ] Very long symbol name (1000+ chars).
- [ ] Versioned symbol: `foo@@GLIBC_2.5` display.

### 7g. Integration Tests
- [ ] Compile `.c` → `.o` with `cc`, run `nm`, verify expected symbols.
- [ ] Compare output against host `nm` on same `.o`.

## 8. Man Page

- [ ] Write `nm.1` covering all flags, output formats, exit codes.
- [ ] Document symbol type letters in a table.
- [ ] Install to `$(DESTDIR)/usr/share/man/man1/`.
