# `usr.bin/objcopy` Tasklist

Goal: implement `objcopy` transformations through `libelfobj` object model/writer.

---

## 1. `libelfobj` Integration

- [ ] Read input via `elf_open()` or `elf_open_with_options()`.
- [ ] Create mutable output object via `elf_create()` matching input class/endian/machine.
- [ ] Copy sections using `elf_add_section()` + `elf_section_set_data()`.
- [ ] Copy symbols using `elf_add_symbol()`.
- [ ] Copy relocations using `elf_add_reloc()`.
- [ ] Copy program headers for ET_EXEC/ET_DYN via segment APIs.
- [ ] Finalize with `elf_write_file()`.
- [ ] Call `elf_close()` on both input and output objects.

## 2. Section Operations

### 2a. Remove
- [ ] `--remove-section=<name>` / `-R <name>`: omit named section from output.
- [ ] Remove multiple sections: accept `-R` multiple times.
- [ ] Reject removal of critical sections (`.shstrtab`, `.strtab` when symbols remain).
- [ ] Update all section indices, symbol section references, and relocation section links after removal.

### 2b. Rename
- [ ] `--rename-section <old>=<new>[,flags]`: rename section, optionally change flags.
- [ ] Validate the new name is ≤ 65535 bytes (ELF strtab limit).
- [ ] Update relocations and group sections referencing the renamed section.

### 2c. Set Flags
- [ ] `--set-section-flags <name>=<flags>`: set `alloc`, `load`, `readonly`, `code`, `data`, `rom`, `contents`.
- [ ] Map flag names to `SHF_*` values.

### 2d. Copy / Keep / Only
- [ ] `--only-keep-debug`: keep only debug sections + section headers (for `.debug` split).
- [ ] `--only-section=<name>` / `-j <name>`: copy only the named section(s).
- [ ] `--add-section <name>=<file>`: read file content and add as a new section.
- [ ] `--update-section <name>=<file>`: replace content of existing section.
- [ ] `--dump-section <name>=<file>`: extract section content to file.

### 2e. Alignment and Integrity
- [ ] Preserve `sh_addralign` on all copied sections.
- [ ] Preserve `sh_entsize` on fixed‑entry sections (`.symtab`, `.dynsym`, `.rel*`).
- [ ] Preserve `SHF_GROUP` / COMDAT group membership.
- [ ] Preserve `SHF_MERGE` / `SHF_STRINGS` properties.
- [ ] Preserve TLS flag (`SHF_TLS`) and TLS segment mapping.

## 3. Symbol Operations

- [ ] `--strip-all` / `-S`: remove all symbols not needed for relocation.
- [ ] `--strip-debug` / `-g`: remove debug symbols only (`STT_FILE`, `N` type).
- [ ] `--strip-unneeded`: remove symbols not referenced by relocations.
- [ ] `--keep-symbol=<name>` / `-K`: keep specific symbol even when stripping.
- [ ] `--keep-symbols=<file>`: read keep‑list from file.
- [ ] `--localize-symbol=<name>` / `-L`: convert global to local.
- [ ] `--localize-hidden`: localize all hidden/internal visibility symbols.
- [ ] `--globalize-symbol=<name>`: convert local to global.
- [ ] `--weaken-symbol=<name>` / `-W`: convert global to weak.
- [ ] `--weaken`: weaken all global symbols.
- [ ] `--redefine-sym <old>=<new>`: rename symbol.
- [ ] `--redefine-syms=<file>`: batch rename from file.
- [ ] `--strip-symbol=<name>` / `-N`: remove specific symbol.
- [ ] `--keep-file-symbols`: retain `STT_FILE` symbols during strip.
- [ ] Preserve `.dynsym` entries in ET_DYN/ET_EXEC (never strip dynamic symbols).

## 4. Binary Conversion Modes

- [ ] `--input-target=binary` / `-I binary`: read raw binary, create ELF with `.data` section.
  - [ ] Generate `_binary_<name>_start`, `_binary_<name>_end`, `_binary_<name>_size` symbols.
  - [ ] Default machine/class from `--output-target` or host.
- [ ] `--output-target=binary` / `-O binary`: extract loadable segments as raw binary.
  - [ ] Flatten in VMA order with padding for gaps.
- [ ] `--output-target=ihex` / `-O ihex`: Intel HEX output (if needed by embedded flow).
- [ ] `--output-target=srec` / `-O srec`: Motorola S‑record output (if needed).

## 5. Miscellaneous Flags

- [ ] `--gap-fill=<val>`: fill inter‑section gaps with the given byte.
- [ ] `--pad-to=<addr>`: pad output to address.
- [ ] `--set-start=<addr>`: set entry point.
- [ ] `--adjust-start=<incr>`: adjust entry point.
- [ ] `--change-addresses=<incr>`: adjust all addresses.
- [ ] `--change-section-address <name>=<addr>` / `{+,-}<incr>`.
- [ ] `--change-section-lma <name>=<addr>` / `{+,-}<incr>`.
- [ ] `--change-section-vma <name>=<addr>` / `{+,-}<incr>`.
- [ ] `--add-gnu-debuglink=<file>`: add `.gnu_debuglink` section with CRC.
- [ ] `--compress-debug-sections={zlib,zstd}`: compress DWARF sections.
- [ ] `--decompress-debug-sections`: decompress DWARF sections.
- [ ] `--enable-deterministic-archives` / `-D`: when writing archives, use deterministic mode.
- [ ] `--preserve-dates` / `-p`: preserve input file mtime on output.

## 6. Deterministic Output

- [ ] Ensure section ordering in output matches input (no reordering unless explicitly requested).
- [ ] Ensure string table entries are emitted in deterministic order.
- [ ] Ensure symbol table order is stable (locals before globals, otherwise input order).

## 7. Error Handling

- [ ] Input not ELF: clear error message, exit 1.
- [ ] Removing section that doesn't exist: warn, continue.
- [ ] Renaming section that doesn't exist: warn, continue.
- [ ] Symbol not found for `--keep-symbol` / `--strip-symbol`: warn, continue.
- [ ] Conflicting options (e.g. `--strip-all` + `--keep-symbol`): resolve keep > strip.
- [ ] Output write failure: remove partial output file, exit 1.
- [ ] Report `elf_last_diagnostics()` on any `libelfobj` error.

## 8. Build System

- [ ] Create `Makefile` linking `libelfobj.a`.
- [ ] `NATIVE_BUILD=1` support.
- [ ] `install` to `$(DESTDIR)/usr/bin/objcopy`.

## 9. Testing

### 9a. Section Operation Tests
- [ ] Remove `.comment` section — verify absent in output, other sections intact.
- [ ] Rename `.text` to `.mytext` — verify name, verify relocation references update.
- [ ] `--only-section=.text` — output contains only `.text` + required structural sections.
- [ ] `--add-section=.note.foo=data.bin` — verify section present with correct content.
- [ ] `--set-section-flags .data=readonly` — verify `SHF_WRITE` cleared.

### 9b. Symbol Operation Tests
- [ ] `--strip-debug` on ET_REL — `STT_FILE` symbols removed, globals remain.
- [ ] `--strip-all` on ET_REL — only required relocation symbols remain.
- [ ] `--localize-symbol=foo` — `foo` becomes `STB_LOCAL`.
- [ ] `--weaken` — all globals become weak.
- [ ] `--redefine-sym old=new` — symbol renamed in `.symtab` and `.strtab`.

### 9c. Binary Conversion Tests
- [ ] Raw binary → ELF → raw binary round‑trip — byte‑identical.
- [ ] Verify `_binary_*` symbols generated with correct values.
- [ ] Binary output from ET_EXEC with two `PT_LOAD` segments — verify gap fill.

### 9d. Integrity Tests
- [ ] Copied ET_REL: can be linked by `ld` into an executable.
- [ ] Copied ET_DYN: passes `readelf -a` validation and can be `dlopen()`'d.
- [ ] Copied ET_EXEC: runs correctly.
- [ ] ELF32 and ELF64 objects both copy correctly.

### 9e. Edge Cases
- [ ] Very large object (>100 sections).
- [ ] Object with COMDAT groups — groups preserved.
- [ ] Object with compressed debug sections — pass‑through or decompress+recompress.
- [ ] Input and output are the same file — handle via temp file + rename.

## 10. Man Page

- [ ] Write `objcopy.1` covering all section/symbol/binary operations.
- [ ] Document common recipes (strip debug, split debuginfo, binary embed).
- [ ] Install to `$(DESTDIR)/usr/share/man/man1/`.
