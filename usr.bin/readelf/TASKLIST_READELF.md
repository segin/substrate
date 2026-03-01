# `usr.bin/readelf` Tasklist

Goal: implement a full-featured `readelf` utility using `libelfobj` for all ELF parsing. Support i386, x86-64, ARMv7, and AArch64 with complete display of architecture-specific structures.

---

## 1. `libelfobj` Integration

- [x] Open input with `elf_open()` / `elf_open_file()`.
- [x] Handle ELF32 and ELF64 transparently.
- [x] Handle both little-endian and big-endian objects.
- [x] Accept multiple input files on command line.
- [x] Report errors for non-ELF input without crashing.
- [x] Handle truncated/malformed ELF with bounds-safe reads.

## 2. ELF Header Display (`-h` / `--file-header`)

- [x] Display `e_ident` magic bytes.
- [x] Display class: `ELF32` / `ELF64`.
- [x] Display data encoding: `2's complement, little endian` / `big endian`.
- [x] Display ELF version.
- [x] Display OS/ABI: `ELFOSABI_NONE`/`LINUX`/`FREEBSD`/`ARM`/etc. with numeric fallback.
- [x] Display ABI version.
- [x] Display type: `REL`/`EXEC`/`DYN`/`CORE` with numeric fallback.
- [x] Display machine: `Intel 80386` / `Advanced Micro Devices X86-64` / `ARM` / `AArch64` / numeric fallback for unknown.
- [x] Display version.
- [x] Display entry point address.
- [x] Display program header offset, section header offset.
- [x] Display flags with arch-specific decoding:
    - [x] ARM: `EF_ARM_ABI_VER5`, `EF_ARM_ABI_FLOAT_HARD`/`SOFT`, `EF_ARM_BE8`, `EF_ARM_INTERWORK`.
    - [x] AArch64: flags (typically 0).
    - [x] x86: flags (typically 0).
- [x] Display header sizes (ELF header, program header entry, section header entry).
- [x] Display counts (program headers, section headers, section name string table index).

## 3. Section Headers Display (`-S` / `--section-headers` / `--sections`)

- [x] Tabular display: index, name, type, address, offset, size, entsize, flags, link, info, align.
- [x] Decode section type names: `SHT_NULL`, `SHT_PROGBITS`, `SHT_SYMTAB`, `SHT_STRTAB`, `SHT_RELA`, `SHT_HASH`, `SHT_DYNAMIC`, `SHT_NOTE`, `SHT_NOBITS`, `SHT_REL`, `SHT_DYNSYM`, `SHT_INIT_ARRAY`, `SHT_FINI_ARRAY`, `SHT_PREINIT_ARRAY`, `SHT_GROUP`, `SHT_SYMTAB_SHNDX`, `SHT_GNU_HASH`, `SHT_GNU_verdef`, `SHT_GNU_verneed`, `SHT_GNU_versym`.
- [x] ARM-specific types: `SHT_ARM_EXIDX` (ARM_EXIDX), `SHT_ARM_PREEMPTMAP`, `SHT_ARM_ATTRIBUTES` (ARM_ATTRIBUTES).
- [x] AArch64-specific types: `SHT_AARCH64_ATTRIBUTES`.
- [x] Decode flags: `W` (WRITE), `A` (ALLOC), `X` (EXECINSTR), `M` (MERGE), `S` (STRINGS), `I` (INFO_LINK), `L` (LINK_ORDER), `O` (OS_NONCONFORMING), `G` (GROUP), `T` (TLS), `C` (COMPRESSED), `o` (OS-specific), `E` (EXCLUDE), `p` (processor-specific).
- [x] ARM: `SHF_ARM_PURECODE` flag as `y`.
- [x] Flag legend at bottom of display.
- [x] With `-W` (wide): do not truncate section names.

## 4. Program Headers Display (`-l` / `--program-headers` / `--segments`)

- [x] Display each program header: type, offset, vaddr, paddr, filesz, memsz, flags, align.
- [x] Decode segment type: `PT_NULL`, `PT_LOAD`, `PT_DYNAMIC`, `PT_INTERP`, `PT_NOTE`, `PT_SHLIB`, `PT_PHDR`, `PT_TLS`, `PT_GNU_EH_FRAME`, `PT_GNU_STACK`, `PT_GNU_RELRO`, `PT_GNU_PROPERTY`.
- [x] ARM-specific: `PT_ARM_EXIDX`.
- [x] AArch64-specific: `PT_AARCH64_MEMTAG_MTE`.
- [x] Decode flags: `R`/`W`/`E`.
- [x] Section-to-segment mapping table.
- [x] Display interpreter path for `PT_INTERP`.

## 5. Symbol Tables (`-s` / `--syms` / `--symbols`)

- [x] Display `.symtab` and `.dynsym` tables.
- [x] Columns: num, value, size, type, bind, vis, ndx, name.
- [x] Decode symbol type: `NOTYPE`, `OBJECT`, `FUNC`, `SECTION`, `FILE`, `COMMON`, `TLS`, `GNU_IFUNC`.
- [x] Decode binding: `LOCAL`, `GLOBAL`, `WEAK`, `GNU_UNIQUE`.
- [x] Decode visibility: `DEFAULT`, `HIDDEN`, `PROTECTED`, `INTERNAL`.
- [x] Decode section index: `UND`, `ABS`, `COM`, or numeric.
- [x] ARM: note Thumb bit in symbol value (bit 0) for `STT_FUNC` symbols.
- [x] ARM: identify mapping symbols (`$a`, `$t`, `$d`) in display.
- [x] AArch64: identify mapping symbols (`$x`, `$d`).
- [x] Handle `SHT_SYMTAB_SHNDX` extended section indices.
- [x] `--dyn-syms`: display only `.dynsym`.

## 6. Relocation Tables (`-r` / `--relocs`)

- [x] Display all `SHT_REL` and `SHT_RELA` sections.
- [x] Columns: offset, info, type, sym.value, sym.name + addend.
- [x] Decode relocation type names per architecture:

### 6a. i386 Relocation Names
- [x] `R_386_NONE`, `R_386_32`, `R_386_PC32`, `R_386_GOT32`, `R_386_PLT32`, `R_386_COPY`, `R_386_GLOB_DAT`, `R_386_JMP_SLOT`, `R_386_RELATIVE`, `R_386_GOTOFF`, `R_386_GOTPC`, `R_386_GOT32X`.
- [x] `R_386_TLS_TPOFF`, `R_386_TLS_IE`, `R_386_TLS_GOTIE`, `R_386_TLS_LE`, `R_386_TLS_GD`, `R_386_TLS_LDM`, `R_386_TLS_LDO_32`, `R_386_TLS_DTPMOD32`, `R_386_TLS_DTPOFF32`.
- [x] `R_386_16`, `R_386_PC16`, `R_386_8`, `R_386_PC8`, `R_386_SIZE32`.
- [x] `R_386_IRELATIVE`.

### 6b. x86-64 Relocation Names
- [x] `R_X86_64_NONE`, `R_X86_64_64`, `R_X86_64_PC32`, `R_X86_64_GOT32`, `R_X86_64_PLT32`, `R_X86_64_COPY`, `R_X86_64_GLOB_DAT`, `R_X86_64_JUMP_SLOT`, `R_X86_64_RELATIVE`.
- [x] `R_X86_64_GOTPCREL`, `R_X86_64_32`, `R_X86_64_32S`, `R_X86_64_16`, `R_X86_64_PC16`, `R_X86_64_8`, `R_X86_64_PC8`.
- [x] `R_X86_64_DTPMOD64`, `R_X86_64_DTPOFF64`, `R_X86_64_TPOFF64`, `R_X86_64_TLSGD`, `R_X86_64_TLSLD`, `R_X86_64_DTPOFF32`, `R_X86_64_GOTTPOFF`, `R_X86_64_TPOFF32`.
- [x] `R_X86_64_PC64`, `R_X86_64_GOTOFF64`, `R_X86_64_GOTPC32`, `R_X86_64_SIZE32`, `R_X86_64_SIZE64`.
- [x] `R_X86_64_GOTPCRELX`, `R_X86_64_REX_GOTPCRELX`, `R_X86_64_IRELATIVE`.

### 6c. ARM Relocation Names
- [x] All `R_ARM_*` types (0–111, 160, 249–255) with human-readable names.
- [x] Key types: `R_ARM_ABS32`, `R_ARM_REL32`, `R_ARM_CALL`, `R_ARM_JUMP24`, `R_ARM_THM_CALL`, `R_ARM_THM_JUMP24`, `R_ARM_MOVW_ABS_NC`, `R_ARM_MOVT_ABS`, `R_ARM_PREL31`, `R_ARM_GOT_BREL`, `R_ARM_PLT32`, `R_ARM_TLS_GD32`, `R_ARM_TLS_IE32`, `R_ARM_V4BX`, `R_ARM_TARGET1`, `R_ARM_TARGET2`.

### 6d. AArch64 Relocation Names
- [x] All `R_AARCH64_*` types with human-readable names.
- [x] Key types: `R_AARCH64_ABS64`, `R_AARCH64_ABS32`, `R_AARCH64_PREL32`, `R_AARCH64_ADR_PREL_PG_HI21`, `R_AARCH64_ADD_ABS_LO12_NC`, `R_AARCH64_LDST*_ABS_LO12_NC`, `R_AARCH64_JUMP26`, `R_AARCH64_CALL26`, `R_AARCH64_GLOB_DAT`, `R_AARCH64_JUMP_SLOT`, `R_AARCH64_RELATIVE`, `R_AARCH64_TLS*`, `R_AARCH64_TLSDESC*`, `R_AARCH64_IRELATIVE`.
- [x] Unknown relocation type: print numeric value.

## 7. Dynamic Section (`-d` / `--dynamic`)

- [x] Display all `DT_*` tags from `.dynamic` section.
- [x] Decode tag names: `DT_NEEDED`, `DT_SONAME`, `DT_RPATH`, `DT_RUNPATH`, `DT_INIT`, `DT_FINI`, `DT_INIT_ARRAY`, `DT_FINI_ARRAY`, `DT_HASH`, `DT_GNU_HASH`, `DT_STRTAB`, `DT_SYMTAB`, `DT_STRSZ`, `DT_SYMENT`, `DT_PLTGOT`, `DT_PLTRELSZ`, `DT_PLTREL`, `DT_JMPREL`, `DT_REL`/`DT_RELA`, `DT_RELSZ`/`DT_RELASZ`, `DT_RELENT`/`DT_RELAENT`, `DT_TEXTREL`, `DT_BIND_NOW`, `DT_FLAGS`, `DT_FLAGS_1`, `DT_VERNEED`, `DT_VERNEEDNUM`, `DT_VERDEF`, `DT_VERDEFNUM`, `DT_VERSYM`, `DT_DEBUG`, `DT_NULL`.
- [x] Decode `DT_FLAGS` bits: `DF_ORIGIN`, `DF_SYMBOLIC`, `DF_TEXTREL`, `DF_BIND_NOW`, `DF_STATIC_TLS`.
- [x] Decode `DT_FLAGS_1` bits: `DF_1_NOW`, `DF_1_GLOBAL`, `DF_1_NODELETE`, `DF_1_LOADFLTR`, `DF_1_INITFIRST`, `DF_1_NOOPEN`, `DF_1_ORIGIN`, `DF_1_INTERPOSE`, `DF_1_NODEFLIB`, `DF_1_NODUMP`, `DF_1_PIE`.
- [x] Resolve string values for `DT_NEEDED`, `DT_SONAME`, `DT_RPATH`, `DT_RUNPATH` from `.dynstr`.

## 8. Notes Display (`-n` / `--notes`)

- [x] Display all `PT_NOTE` segments and `SHT_NOTE` sections.
- [x] Decode note name + type combinations:
    - [x] `GNU` + `NT_GNU_ABI_TAG` (1): display OS/ABI and version.
    - [x] `GNU` + `NT_GNU_HWCAP` (2): display hardware capabilities.
    - [x] `GNU` + `NT_GNU_BUILD_ID` (3): display build ID as hex.
    - [x] `GNU` + `NT_GNU_GOLD_VERSION` (4): display gold version string.
    - [x] `GNU` + `NT_GNU_PROPERTY_TYPE_0` (5): decode `.note.gnu.property`.
- [x] Decode GNU property types:
    - [x] `GNU_PROPERTY_STACK_SIZE`.
    - [x] `GNU_PROPERTY_NO_COPY_ON_PROTECTED`.
    - [x] `GNU_PROPERTY_X86_ISA_1_NEEDED` / `_USED`: decode ISA level bits (BASELINE, V2, V3, V4).
    - [x] `GNU_PROPERTY_X86_FEATURE_1_AND`: decode IBT, SHSTK bits.
    - [x] `GNU_PROPERTY_AARCH64_FEATURE_1_AND`: decode BTI, PAC bits.
- [x] Display unknown note types with hex dump.
- [x] Handle alignment (4-byte for 32-bit, 8-byte for 64-bit) correctly.

## 9. Version Information (`-V` / `--version-info`)

- [x] Display `.gnu.version` (version symbol indices per dynsym entry).
- [x] Display `.gnu.version_d` (version definitions): index, flags, version name, predecessors.
- [x] Display `.gnu.version_r` (version requirements): file, version name, flags.
- [x] Cross-reference version indices to definition/requirement names.

## 10. Hash Tables (`--histogram` / `-I`)

- [x] Decode SYSV hash table (`.hash`): nbuckets, nchain, bucket distribution histogram.
- [x] Decode GNU hash table (`.gnu.hash`): nbuckets, symndx, maskwords, shift2, bloom filter size, bucket distribution.
- [x] Display chain length statistics and histogram.

## 11. Section Groups (`-g` / `--section-groups`)

- [ ] Display `SHT_GROUP` sections.
- [ ] Decode group flags: `GRP_COMDAT`.
- [ ] List member section indices and names.

## 12. ARM-Specific Displays

### 12a. ARM Unwind Tables (`-u` / `--unwind`)
- [ ] Decode `.ARM.exidx` entries: function offset and personality routine index.
- [ ] Display each entry's unwind opcodes.
- [ ] Decode compact unwind instructions: `vsp = vsp + N`, `pop {rN-rM}`, `finish`, etc.
- [ ] Cross-reference with `.ARM.extab` for extended unwind data.
- [ ] Handle `EXIDX_CANTUNWIND` entries.

### 12b. ARM Build Attributes (`-A` / `--arch-specific`)
- [ ] Parse and display `.ARM.attributes` (`SHT_ARM_ATTRIBUTES`) section.
- [ ] Decode vendor subsections (primarily `aeabi`).
- [ ] Decode all standard tags:
    - [ ] `Tag_CPU_name` (4), `Tag_CPU_arch` (6), `Tag_CPU_arch_profile` (7).
    - [ ] `Tag_ARM_ISA_use` (8), `Tag_THUMB_ISA_use` (9).
    - [ ] `Tag_FP_arch` (10), `Tag_WMMX_arch` (11), `Tag_Advanced_SIMD_arch` (12).
    - [ ] `Tag_ABI_PCS_*` (13–17), `Tag_ABI_PCS_wchar_t` (18).
    - [ ] `Tag_ABI_FP_*` (19–23), `Tag_ABI_align_*` (24–25).
    - [ ] `Tag_ABI_enum_size` (26), `Tag_ABI_HardFP_use` (27), `Tag_ABI_VFP_args` (28).
    - [ ] `Tag_CPU_unaligned_access` (34), `Tag_FP_HP_extension` (36).
    - [ ] `Tag_MPExtension_use` (42), `Tag_DIV_use` (44), `Tag_DSP_extension` (46).
    - [ ] `Tag_Virtualization_use` (68).
- [ ] Display tag values with human-readable interpretations (e.g., `Tag_CPU_arch: v7` not just `10`).
- [ ] Handle unknown vendor subsections gracefully.

## 13. Hex/String Dump (`-x` / `-p`)

- [ ] `-x <section>` / `--hex-dump=<section>`: hex dump of section contents.
- [ ] `-p <section>` / `--string-dump=<section>`: print strings from section.
- [ ] Accept section by name or by index number.
- [ ] Bounds-safe: do not read beyond section size.
- [ ] Display offset column in hex dump.

## 14. DWARF Debug Display (`-w` / `--debug-dump`)

- [ ] `--debug-dump=info`: display `.debug_info` DIE tree.
- [ ] `--debug-dump=abbrev`: display `.debug_abbrev` tables.
- [ ] `--debug-dump=line`: display `.debug_line` line number programs.
- [ ] `--debug-dump=frames`: display `.debug_frame` and `.eh_frame` CIE/FDE.
- [ ] `--debug-dump=ranges`: display `.debug_ranges` and `.debug_rnglists`.
- [ ] `--debug-dump=str`: display `.debug_str` string table.
- [ ] `--debug-dump=aranges`: display `.debug_aranges`.
- [ ] `--debug-dump=loc`: display `.debug_loc` and `.debug_loclists`.
- [ ] Handle DWARF versions 2, 3, 4, and 5.
- [ ] Handle compressed debug sections (`SHF_COMPRESSED` and `.zdebug_*`).

## 15. Core File Display (`-c` option or auto-detect `ET_CORE`)

- [ ] Display core file notes: `NT_PRSTATUS`, `NT_PRPSINFO`, `NT_FPREGSET`, `NT_AUXV`, `NT_FILE`.
- [ ] Decode register sets per architecture.
- [ ] Display signal information from `NT_PRSTATUS`.
- [ ] Display mapped files from `NT_FILE`.

## 16. Display Options

- [ ] `-a` / `--all`: equivalent to `-h -l -S -s -r -d -V -A -I`.
- [ ] `-W` / `--wide`: do not truncate output to 80 columns.
- [ ] `-e` / `--headers`: equivalent to `-h -l -S`.
- [ ] `-t` / `--section-details`: more detailed section header display.
- [ ] `--dyn-syms`: display only dynamic symbol table.
- [ ] `-C` / `--demangle`: demangle C++ symbol names (via `libdemangle`).
- [ ] `-D` / `--use-dynamic`: use dynamic symbol table for symbol display.
- [ ] `--sym-base=0|8|10|16`: control symbol value radix.
- [ ] `--print-sysv`: sysv-style output for some displays.

## 17. Error Handling

- [ ] Non-ELF input: `readelf: Error: Not an ELF file`, exit 1.
- [ ] Truncated ELF: display what is possible, warn about truncation.
- [ ] Multiple files: print filename header before each file's output.
- [ ] Section index out of range: warn, skip.
- [ ] String table index out of range: display `<corrupt>`.
- [ ] Unknown machine types: display numeric values.
- [ ] Unknown section/segment/relocation types: display numeric values.

## 18. Build System

- [ ] Create `Makefile` linking `libelfobj.a` and `libdemangle.a`.
- [ ] `NATIVE_BUILD=1` support.
- [ ] `install` to `$(DESTDIR)/usr/bin/readelf`.

## 19. Testing

### 19a. Header Tests
- [ ] Display headers for i386, x86-64, ARMv7, AArch64 objects.
- [ ] ARM `e_flags` decoded correctly (EABI version, float ABI).
- [ ] Verify all field values against known-good `readelf -h` output.

### 19b. Section/Segment Tests
- [ ] All section types display with correct names.
- [ ] ARM-specific: `SHT_ARM_EXIDX`, `SHT_ARM_ATTRIBUTES` displayed.
- [ ] AArch64-specific: `SHT_AARCH64_ATTRIBUTES` displayed.
- [ ] `PT_ARM_EXIDX` segment displayed.
- [ ] Section-to-segment mapping correct.

### 19c. Symbol Tests
- [ ] `.symtab` and `.dynsym` both displayed.
- [ ] ARM Thumb bit displayed correctly.
- [ ] Mapping symbols (`$a`/`$t`/`$d`/`$x`) visible.
- [ ] C++ demangling with `-C` works.

### 19d. Relocation Tests
- [ ] All i386 relocation type names correct.
- [ ] All x86-64 relocation type names correct.
- [ ] All ARM relocation type names correct.
- [ ] All AArch64 relocation type names correct.
- [ ] Unknown types: numeric display.

### 19e. Dynamic Tests
- [ ] All `DT_*` tags decoded.
- [ ] `DT_FLAGS` and `DT_FLAGS_1` bit decoding.
- [ ] String resolution from `.dynstr`.

### 19f. Notes Tests
- [ ] GNU build-id displayed as hex.
- [ ] x86 ISA level property decoded.
- [ ] AArch64 BTI/PAC property decoded.
- [ ] Unknown notes display hex dump.

### 19g. ARM-Specific Tests
- [ ] ARM unwind table decoded.
- [ ] ARM build attributes: all standard tags decoded with human-readable values.
- [ ] `Tag_CPU_arch` shows architecture name, not just number.

### 19h. Compatibility Tests
- [ ] Output matches GNU `readelf` for same input on key sections.
- [ ] Output parseable by scripts that consume `readelf -a` output.

### 19i. Edge Cases / Fuzz
- [ ] Truncated ELF: no crash, partial display with warnings.
- [ ] Zero-length sections: handled.
- [ ] Very large symbol tables (100k+ symbols): displayed.
- [ ] Fuzz harness: crash-free on arbitrary input.

## 20. Man Page

- [ ] Write `readelf.1` covering all options and per-architecture details.
- [ ] Install to `$(DESTDIR)/usr/man/man1/`.
