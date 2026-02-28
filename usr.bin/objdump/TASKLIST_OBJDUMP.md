# `usr.bin/objdump` Tasklist

Goal: implement `objdump` with ELF structure loading from `libelfobj`.

---

## 1. `libelfobj` Integration

- [ ] Open input files with `elf_open()` / `elf_open_with_options()`.
- [ ] Use `elf_class()`, `elf_endian()`, `elf_machine()`, `elf_type()` for file header display.
- [ ] Use `elf_entry()`, `elf_flags()` for header fields.
- [ ] Iterate sections via `elf_section_count()` / `elf_section_get()`.
- [ ] Query section properties: `elf_section_name()`, `elf_section_type()`, `elf_section_flags()`, `elf_section_addr()`, `elf_section_offset()`, `elf_section_size()`, `elf_section_align()`, `elf_section_entsize()`.
- [ ] Read section data with `elf_section_data()`.
- [ ] Iterate symbols via `elf_symbol_count()` / `elf_symbol_get()`.
- [ ] Iterate relocations via `elf_reloc_count()` / `elf_reloc_get()`.
- [ ] Iterate program headers via segment APIs.
- [ ] Call `elf_close()` on all handles.

## 2. Header Displays

### 2a. File Header (`-f`)
- [ ] Print ELF class, endianness, version, OS/ABI, type, machine, entry point, flags.
- [ ] Print section header / program header table offsets and counts.
- [ ] Format: match `readelf -h` style.

### 2b. Section Headers (`-h`)
- [ ] Print table: Idx, Name, Size, VMA, LMA, File off, Algn, Flags.
- [ ] Decode section flags to readable strings (`CONTENTS`, `ALLOC`, `LOAD`, `CODE`, `DATA`, `READONLY`).
- [ ] Suppress structural sections (`.shstrtab`, `.strtab`, `.symtab`) unless `-a` (all headers).

### 2c. All Headers (`-x`)
- [ ] Equivalent to `-f -h -p` (file + section + program headers).

### 2d. Program Headers (`-p` / `--private-headers`)
- [ ] Print segment table: Type, Offset, VirtAddr, PhysAddr, FileSiz, MemSiz, Flags, Align.
- [ ] Decode segment types (`PT_LOAD`, `PT_DYNAMIC`, `PT_NOTE`, `PT_TLS`, etc.).
- [ ] Print `DYNAMIC` section entries if present.
- [ ] Print `NOTE` segments/sections.

## 3. Symbol Display (`-t` / `--syms`)

- [ ] Print symbol table in tabular format: Value, Flags, Section, Alignment/Size, Name.
- [ ] Decode bind (global/local/weak), type (func/object/notype/file/section), visibility.
- [ ] Handle both `.symtab` and `.dynsym` (print both, label which table).
- [ ] Handle versioned symbols: append `@VERSION` or `@@VERSION`.

## 4. Relocation Display (`-r` / `--reloc`, `-R` / `--dynamic-reloc`)

- [ ] `-r`: display relocations from `.rel*` / `.rela*` sections.
- [ ] `-R`: display relocations from dynamic relocation sections only.
- [ ] Print: Offset, Type, Value, Addend (for RELA), Symbol Name.
- [ ] Decode relocation type names per machine (`R_386_32`, `R_X86_64_64`, etc.) using `libelfobj` reloc helpers.

## 5. Section Content Dump (`-s` / `--full-contents`)

- [ ] Hex dump each section's raw data.
- [ ] Format: offset column + hex bytes (16 per line) + ASCII printable column.
- [ ] Only dump sections with `SHF_ALLOC` by default; all sections with `-j <name>`.
- [ ] `-j <name>`: restrict dump/disassembly to specific section(s).

## 6. Disassembly (`-d` / `-D`)

### 6a. Architecture Backend Interface
- [ ] Define `struct disasm_backend` with `disassemble(const uint8_t *code, size_t len, uint64_t addr, char *buf, size_t bufsz)` returning instruction length.
- [ ] Implement i386 backend (minimum: common opcodes for kernel/userland code).
- [ ] Stub x86_64 backend.
- [ ] Select backend based on `elf_machine()`.
- [ ] Report unsupported machine with diagnostic.

### 6b. `-d` (Disassemble Executable Sections)
- [ ] Disassemble sections with `SHF_EXECINSTR`.
- [ ] Interleave symbol labels at corresponding addresses.
- [ ] Print relocation annotations inline (e.g. `# R_386_32 <sym>`).
- [ ] Print address, hex bytes, mnemonic.

### 6c. `-D` (Disassemble All Sections)
- [ ] Disassemble all sections regardless of flags.
- [ ] Useful for data sections with embedded code.

### 6d. `-S` (Intermix Source)
- [ ] Requires DWARF line info — defer unless `libelfobj` DWARF support is available.
- [ ] Read `.debug_line` to map addresses to source lines.

## 7. Additional Flags

- [ ] `-a` / `--archive-headers`: display archive member headers for `.a` inputs.
- [ ] `--start-address=<addr>` / `--stop-address=<addr>`: restrict display range.
- [ ] `-C` / `--demangle`: C++ name demangling (defer).
- [ ] `-w` / `--wide`: don't truncate long lines.
- [ ] `--no-show-raw-insn`: hide raw bytes in disassembly.
- [ ] `--prefix-addresses`: print full address before each line.
- [ ] `--insn-width=<n>`: set max instruction width for alignment.

## 8. Archive Traversal

- [ ] Detect `!<arch>\n` magic and iterate members.
- [ ] Print `\n<archive>(<member>):     file format ...\n` banner per member.
- [ ] Parse each member via `elf_open_memory()`.
- [ ] Skip non‑ELF members with diagnostic.
- [ ] Support thin archives (resolve member paths from disk).

## 9. Error Handling

- [ ] Non‑ELF input: clear error, exit 1.
- [ ] Unsupported machine for disassembly: warn, skip disassembly, still do headers/symbols.
- [ ] Truncated section data: warn, print what's available.
- [ ] Corrupt symbol/relocation table: warn via `elf_last_diagnostics()`, continue.

## 10. Build System

- [ ] Create `Makefile` linking `libelfobj.a`.
- [ ] Separate disassembly backends into `disasm/` subdirectory.
- [ ] `NATIVE_BUILD=1` for host testing.
- [ ] `install` to `$(DESTDIR)/usr/bin/objdump`.

## 11. Testing

### 11a. Header Tests
- [ ] `-f` on ET_REL, ET_EXEC, ET_DYN — verify all header fields.
- [ ] `-h` — verify section count, names, sizes, flags.
- [ ] `-p` on ET_EXEC — verify segment listing.

### 11b. Symbol / Relocation Tests
- [ ] `-t` on ET_REL — verify globals, locals, weak, undefined.
- [ ] `-r` on ET_REL — verify relocation entries and type names.
- [ ] `-R` on ET_DYN — verify dynamic relocations.
- [ ] Versioned symbols display correctly.

### 11c. Dump Tests
- [ ] `-s -j .text` — verify hex dump matches raw section data.
- [ ] Round‑trip: dump section, compare with `objcopy --dump-section`.

### 11d. Disassembly Tests
- [ ] `-d` on known i386 ET_EXEC — verify known instruction sequences.
- [ ] Symbol labels appear at correct addresses.
- [ ] Relocation annotations appear inline.
- [ ] `-D` disassembles `.data` section.

### 11e. Archive Tests
- [ ] Archive with 3 `.o` files — per‑member banners and correct output.
- [ ] Archive with non‑ELF member — skipped with message.

### 11f. Edge Cases
- [ ] ELF with no sections (only program headers).
- [ ] ELF with no symbol table.
- [ ] Truncated / malformed ELF — graceful error.
- [ ] ELF32 and ELF64 inputs.

## 12. Man Page

- [ ] Write `objdump.1` covering all flags and output formats.
- [ ] Document disassembly backend limitations.
- [ ] Install to `$(DESTDIR)/usr/share/man/man1/`.
