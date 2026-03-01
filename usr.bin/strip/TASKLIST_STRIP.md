# `usr.bin/strip` Tasklist

Goal: implement `strip` as a `libelfobj` transform supporting i386, x86-64, ARMv7, and AArch64 ELF objects.

---

## 1. `libelfobj` Integration

- [ ] Open input with `elf_open()` / `elf_open_memory()`.
- [ ] Create mutable output with `elf_create()` matching input class/endian/machine.
- [ ] Copy sections selectively based on strip mode.
- [ ] Copy program headers for ET_EXEC/ET_DYN unchanged.
- [ ] Finalize and write with `elf_write_file()`.
- [ ] Close both handles.
- [ ] In‑place mode: write to temp file, `rename()` over original.
- [ ] Validate output with `elf_validate()` before final rename.

## 2. Strip Modes

### 2a. `--strip-all` / `-s` (default)
- [ ] Remove `.symtab` and its associated `.strtab`.
- [ ] Remove all `SHT_SYMTAB` sections.
- [ ] Remove debug sections: `.debug_abbrev`, `.debug_info`, `.debug_line`, `.debug_str`, `.debug_ranges`, `.debug_loc`, `.debug_aranges`, `.debug_frame`, `.debug_macro`, `.debug_pubnames`, `.debug_pubtypes`, `.debug_rnglists`, `.debug_loclists`, `.debug_addr`, `.debug_str_offsets`, `.debug_line_str`, `.debug_names`.
- [ ] Remove compressed debug sections: `.zdebug_*` and `SHF_COMPRESSED` `.debug_*`.
- [ ] Remove legacy debug: `.line`, `.stab`, `.stabstr`.
- [ ] Remove `.comment` section.
- [ ] Remove `.note.GNU-stack` and other non‑essential notes.
- [ ] Remove `.note.gnu.gold-version`, `.note.gnu.build-id` (optionally keep with `--keep-section`).
- [ ] Preserve `.dynsym`, `.dynstr`, `.hash`, `.gnu.hash` — required for dynamic linking.
- [ ] Preserve all `SHF_ALLOC` sections.
- [ ] Preserve `.shstrtab` (section header string table).
- [ ] Preserve program headers unchanged.
- [ ] Preserve `.gnu.version`, `.gnu.version_d`, `.gnu.version_r` — version info for dynamic linker.
- [ ] Preserve `.interp` section.

### 2b. `--strip-debug` / `-g` / `-S`
- [ ] Remove only debug sections (`.debug_*`, `.zdebug_*`, `.line`, `.stab*`).
- [ ] Preserve `.symtab` and `.strtab` (useful for `nm`, `gprof`).
- [ ] Preserve `.comment` section.
- [ ] Preserve `.note.gnu.build-id`.

### 2c. `--strip-unneeded`
- [ ] Remove symbols not referenced by any `SHT_REL` / `SHT_RELA` section.
- [ ] Build a set of referenced symbol indices from all relocation entries.
- [ ] Keep `STB_GLOBAL` and `STB_WEAK` defined symbols (needed for linking).
- [ ] Remove `STB_LOCAL` symbols not referenced by relocations.
- [ ] Keep `STT_SECTION` symbols referenced by relocations.
- [ ] For ET_EXEC/ET_DYN: equivalent to `--strip-all` (relocations resolved at link time).

### 2d. `--strip-dwo`
- [ ] Remove `.dwo` / DWARF split debug sections only (`.debug_info.dwo`, `.debug_abbrev.dwo`, `.debug_line.dwo`, `.debug_str.dwo`, `.debug_str_offsets.dwo`, `.debug_macro.dwo`, `.debug_loclists.dwo`, `.debug_rnglists.dwo`).
- [ ] Preserve `.debug_addr` (shared with skeleton).

## 3. Symbol Control Flags

- [ ] `--keep-symbol=<name>` / `-K <name>`: protect specific symbol from removal (accumulates).
- [ ] `--keep-symbols=<file>`: read keep‑list from file (one symbol per line).
- [ ] `--strip-symbol=<name>` / `-N <name>`: force-remove specific symbol (even in `--strip-debug` mode).
- [ ] `--keep-file-symbols`: retain `STT_FILE` symbols.
- [ ] `--discard-all` / `-x`: remove all non‑global symbols.
- [ ] `--discard-locals` / `-X`: remove compiler‑generated local symbols (`.L*` labels on x86/ARM, `$a`/`$t`/`$d`/`$x` mapping symbols on ARM/AArch64).
- [ ] `-w` / `--wildcard`: allow glob patterns in `-K`/`-N` names (`*`, `?`, `[...]`).

## 4. Section Control Flags

- [ ] `--remove-section=<name>` / `-R <name>`: force removal of specific section (accumulates).
- [ ] `--keep-section=<name>`: prevent section removal even in strip‑all mode.
- [ ] `--remove-relocations=<name>`: remove relocation sections associated with named section.

## 5. Output Options

- [ ] `-o <file>` / `--output-file=<file>`: write to different file instead of in‑place.
- [ ] `-p` / `--preserve-dates`: preserve original atime/mtime via `utimes()`.
- [ ] `-D` / `--enable-deterministic-archives`: deterministic mode (zero timestamps if stripping archive members).
- [ ] `-U` / `--disable-deterministic-archives`: non-deterministic mode.
- [ ] `-v` / `--verbose`: print each section/symbol removed.
- [ ] `--info`: print object file format summary instead of stripping.

## 6. In‑Place Safety

- [ ] Write to `<dir>/.<base>.XXXXXX` temporary file in same directory as original.
- [ ] Copy file permissions from original via `fstat()` + `fchmod()`.
- [ ] Copy ownership if running as root via `fchown()`.
- [ ] `rename()` temp over original on success.
- [ ] `unlink()` temp on failure — original is untouched.
- [ ] Handle `strip` on a running executable (rename‑based replacement is safe on Unix).
- [ ] Handle symlinks: strip the target file, not the symlink.

## 7. Section/Program Header Consistency

- [ ] After removing sections: renumber all `sh_link` / `sh_info` references.
- [ ] After removing `.symtab`: update any `SHT_REL`/`SHT_RELA` `sh_link` that pointed to it.
- [ ] Remove orphaned `SHT_REL`/`SHT_RELA` sections whose target section was removed.
- [ ] Verify no `PT_LOAD` segment references a removed section.
- [ ] Recalculate `e_shnum`, `e_shstrndx` in ELF header.
- [ ] Preserve `PT_NOTE`, `PT_GNU_STACK`, `PT_GNU_RELRO`, `PT_GNU_EH_FRAME` segments.
- [ ] Preserve `PT_DYNAMIC` segment and all dynamic section entries.
- [ ] Preserve `.gnu.hash` / `.hash` for dynamic linker symbol lookup.
- [ ] Preserve `.interp` section and `PT_INTERP` segment.
- [ ] Update `e_shoff` and section header table position after layout changes.
- [ ] Ensure file offsets maintain required alignment per section `sh_addralign`.

## 8. Multi-Architecture Support

### 8a. i386 (ELF32, EM_386)
- [ ] Handle REL relocations (no explicit addend) when analyzing symbol references for `--strip-unneeded`.
- [ ] Preserve `.got`, `.got.plt`, `.plt` sections.
- [ ] Preserve `.rel.dyn`, `.rel.plt` sections in ET_DYN/ET_EXEC.

### 8b. x86-64 (ELF64, EM_X86_64)
- [ ] Handle RELA relocations when analyzing symbol references.
- [ ] Preserve `.rela.dyn`, `.rela.plt` sections in ET_DYN/ET_EXEC.
- [ ] Preserve `.note.gnu.property` with x86-64 ISA level markers.
- [ ] Handle `R_X86_64_GOTPCRELX` / `R_X86_64_REX_GOTPCRELX` relocation references.

### 8c. ARMv7 (ELF32, EM_ARM)
- [ ] Handle REL relocations when analyzing symbol references for `--strip-unneeded`.
- [ ] Preserve `.ARM.exidx` sections (exception index table, `SHT_ARM_EXIDX`).
- [ ] Preserve `.ARM.extab` sections (exception table data).
- [ ] Preserve `.ARM.attributes` sections (`SHT_ARM_ATTRIBUTES`).
- [ ] Preserve `PT_ARM_EXIDX` segment.
- [ ] Preserve ARM mapping symbols (`$a`, `$t`, `$d`) unless `--discard-locals` is specified.
- [ ] When removing `.symtab`: preserve mapping symbols in `.dynsym` if dynamic.
- [ ] Handle `SHF_LINK_ORDER` on `.ARM.exidx` — if linked section removed, remove exidx too.
- [ ] Validate `e_flags` preserved unchanged (EABI version, float ABI).
- [ ] Handle Thumb interwork bit in symbol values during relocation analysis.
- [ ] Preserve `.rel.ARM.exidx` relocation sections when `.ARM.exidx` is kept.

### 8d. AArch64 (ELF64, EM_AARCH64)
- [ ] Handle RELA relocations when analyzing symbol references.
- [ ] Preserve `.note.gnu.property` with BTI/PAC feature flags (`GNU_PROPERTY_AARCH64_FEATURE_1_AND`).
- [ ] Preserve AArch64 mapping symbols (`$x`, `$d`) unless `--discard-locals`.
- [ ] Preserve `.rela.dyn`, `.rela.plt` in dynamic objects.
- [ ] Handle `SHT_AARCH64_ATTRIBUTES` if present.

## 9. ET_REL Specifics

- [ ] Relocatable objects use `.symtab` for relocation; `--strip-all` should warn it may break linking.
- [ ] `--strip-debug` is the safe default for ET_REL.
- [ ] `--strip-unneeded` on ET_REL: analyze relocation references to determine keepable symbols.
- [ ] Preserve section groups (SHT_GROUP / COMDAT) intact unless all members removed.
- [ ] Preserve `.eh_frame` and `.eh_frame_hdr` sections and their associated relocation sections.

## 10. Archive (`.a`) Processing

- [ ] Accept static archives as input.
- [ ] Strip each archive member individually.
- [ ] Preserve archive symbol table (`/` or `__.SYMDEF SORTED`) — rebuild if members changed.
- [ ] Preserve archive long name table (`//`).
- [ ] With `-D`: zero member uid/gid/mtime for deterministic output.
- [ ] Write to temp archive, rename on success.

## 11. Error Handling

- [ ] Non‑ELF input: `strip: <file>: file format not recognized`, exit 1.
- [ ] Read‑only file system: report permission error, exit 1.
- [ ] Multiple files: process all, report errors per file, exit 1 if any failed.
- [ ] `--keep-symbol` with nonexistent symbol: no error (be permissive).
- [ ] Stripping an already‑stripped binary: no‑op, output identical to input.
- [ ] Truncated/malformed ELF: report error, skip file.
- [ ] Unsupported ELF machine type: warn, pass through unchanged.
- [ ] Section referenced by program header cannot be removed: warn, keep.

## 12. Build System

- [ ] Create `Makefile` linking `libelfobj.a`.
- [ ] `NATIVE_BUILD=1` support.
- [ ] `install` to `$(DESTDIR)/usr/bin/strip`.

## 13. Testing

### 13a. Strip Mode Tests
- [ ] `--strip-all` on i386 ET_EXEC: `.symtab` removed, `.dynsym` preserved, program runs.
- [ ] `--strip-all` on x86-64 ET_EXEC: same, plus `.note.gnu.property` preserved.
- [ ] `--strip-all` on ARMv7 ET_EXEC: `.ARM.exidx` and `.ARM.attributes` preserved, program runs.
- [ ] `--strip-all` on AArch64 ET_EXEC: `.note.gnu.property` BTI/PAC preserved, program runs.
- [ ] `--strip-all` on ET_DYN (all arches): `.dynsym` preserved, `dlopen()` works.
- [ ] `--strip-debug` on ET_REL (all arches): `.debug_*` removed, `.symtab` preserved, `ld` can link.
- [ ] `--strip-unneeded` on ET_REL: unreferenced locals removed, referenced locals kept.
- [ ] `--strip-dwo`: only `.dwo` sections removed.

### 13b. Symbol Control Tests
- [ ] `-K foo --strip-all`: `foo` survives strip.
- [ ] `-N bar --strip-debug`: `bar` removed even though `.symtab` is kept.
- [ ] `-x`: all locals removed.
- [ ] `-X` on x86: `.L*` labels removed, named locals kept.
- [ ] `-X` on ARM: `$a`/`$t`/`$d` mapping symbols removed, named locals kept.
- [ ] `-X` on AArch64: `$x`/`$d` mapping symbols removed.
- [ ] `-w -K 'func_*'`: wildcard keep pattern works.

### 13c. Section Control Tests
- [ ] `-R .comment`: `.comment` removed.
- [ ] `--keep-section=.note.ABI-tag --strip-all`: note preserved.
- [ ] `-R .ARM.attributes` on ARM object: attributes removed.
- [ ] `--remove-relocations=.text`: `.rel.text` / `.rela.text` removed.

### 13d. Architecture-Specific Integrity Tests
- [ ] Stripped i386 ET_EXEC runs correctly.
- [ ] Stripped x86-64 ET_EXEC runs correctly.
- [ ] Stripped ARMv7 ET_EXEC runs correctly, exceptions still work (`.ARM.exidx` intact).
- [ ] Stripped AArch64 ET_EXEC runs correctly, BTI still enforced.
- [ ] Stripped ET_DYN (all arches) can be dynamically loaded.
- [ ] Stripped ET_REL (`--strip-debug`, all arches) can be linked into a final binary.
- [ ] `readelf -a` reports no errors on stripped output for all arches.
- [ ] `nm` on `--strip-all` output reports "no symbols" for all arches.

### 13e. In‑Place Safety Tests
- [ ] Simulate write failure: original file untouched.
- [ ] Strip read‑only file: error, original untouched.
- [ ] `--preserve-dates`: mtime unchanged after strip.
- [ ] Strip through symlink: target file stripped, symlink intact.

### 13f. Idempotency Tests
- [ ] Strip an already‑stripped binary: output identical to input.
- [ ] Double‑strip: no crash, no further size reduction.

### 13g. Edge Cases
- [ ] ELF with no `.symtab` (already stripped): `--strip-all` is no‑op.
- [ ] ELF with empty `.symtab` (0 entries): removed cleanly.
- [ ] ELF32 and ELF64 inputs (both endiannesses).
- [ ] Very large binary (>100 sections): all processed.
- [ ] Object with COMDAT groups: groups remain consistent after strip.
- [ ] ARM object with `.ARM.exidx` whose linked `.text` is kept: exidx kept.
- [ ] ARM object where `.ARM.exidx` linked section was removed: exidx removed too.
- [ ] Big-endian ARM ELF (BE8): stripped correctly.

### 13h. Archive Tests
- [ ] `strip -g libfoo.a`: each member debug-stripped, archive symbol table rebuilt.
- [ ] `strip -D libfoo.a`: deterministic metadata in archive.

### 13i. Integration Tests
- [ ] `cc -g -o prog prog.c` → `strip -g prog` → `gdb prog` reports "no debug info".
- [ ] `cc -g -o prog prog.c` → `strip prog` → prog still executes.
- [ ] Compare output against host `strip` on same binary.
- [ ] `as` → `ld` → `strip` → execution for each arch.

## 14. Man Page

- [ ] Write `strip.1` covering all modes, flags, and exit codes.
- [ ] Document which sections are removed in each mode.
- [ ] Document per-architecture preserved sections (`.ARM.exidx`, `.note.gnu.property`).
- [ ] Install to `$(DESTDIR)/usr/man/man1/`.
