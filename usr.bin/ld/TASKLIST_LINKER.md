# `usr.bin/ld` — Standalone ELF Linker Tasklist

Goal: replace the current host‑ld wrapper with a native production ELF linker for i386, x86-64, ARMv7, and AArch64. Must produce correct, deterministic output consumable by the Substrate loader and standard POSIX tooling.

---

## 1. Core CLI and Driver

- [ ] `ld [options] file...` baseline invocation.
- [ ] `-o output` output path (default `a.out`).
- [ ] `-m elf_i386` / `-m elf_x86_64` / `-m armelf` / `-m aarch64elf` target selection.
- [ ] Target inference from first input object `e_machine`.
- [ ] `-L dir` library search path (accumulates, ordered).
- [ ] `-l name` library resolution: search for `libname.so` then `libname.a` in `-L` paths.
- [ ] `-T script` linker script specification.
- [ ] `-e symbol` entry point override.
- [ ] `-r` relocatable output (ET_REL partial link).
- [ ] `-shared` shared library output (ET_DYN).
- [ ] `-pie` position-independent executable.
- [ ] `-static` force static linking (no shared lib search).
- [ ] `-Bstatic`/`-Bdynamic` toggle within link command.
- [ ] `--sysroot=dir` prepend to all search paths.
- [ ] `--as-needed`/`--no-as-needed` DT_NEEDED emission control.
- [ ] `-h soname` / `-soname name` set DT_SONAME.
- [ ] `--whole-archive`/`--no-whole-archive` force full archive inclusion.
- [ ] `-Map file` map file output.
- [ ] `--version-script=file` symbol version script.
- [ ] `--dynamic-linker=path` set PT_INTERP.
- [ ] `-z relro`, `-z now`, `-z noexecstack`, `-z execstack`, `-z text`, `-z notext`, `-z lazy`, `-z nodelete`, `-z nodump`, `-z origin`, `-z combreloc`, `-z nocombreloc`, `-z max-page-size=N`, `-z common-page-size=N`.
- [ ] `--gc-sections` / `--no-gc-sections`.
- [ ] `--print-gc-sections`.
- [ ] `--start-group`/`--end-group` (and `-(` / `-)`) for cyclic archive resolution.
- [ ] `--fatal-warnings`, `--no-warnings`, `--warn-common`, `--warn-unresolved`.
- [ ] `-O level` optimization level (0=fast, 1=default, 2=string merge).
- [ ] `--strip-all` / `--strip-debug` / `-s` / `-S`.
- [ ] `--build-id=style` (none/md5/sha1/sha256/uuid/hex).
- [ ] `--hash-style=sysv`/`gnu`/`both`.
- [ ] `--reproduce=tarball` (save inputs for reproducibility).
- [ ] `--threads`/`--no-threads` parallel processing control.
- [ ] `-v`/`--version` version display.
- [ ] `--trace` print input file names as processed.
- [ ] `-y symbol` / `--trace-symbol=symbol` print binding events.
- [ ] Option conflict detection with clear diagnostics.

---

## 2. ELF Input Processing

### 2a. Object Files (ET_REL)
- [ ] Parse ELF32 and ELF64 headers via `libelfobj`.
- [ ] Validate `e_machine` matches target.
- [ ] Read section headers, section name string table.
- [ ] Read `.symtab`/`.strtab` local+global symbols.
- [ ] Read relocations: REL sections (i386/ARM), RELA sections (x86-64/AArch64).
- [ ] Read section groups (COMDAT) and resolve keep/discard.
- [ ] Read `.note.*` sections, `.comment` sections.
- [ ] Read `.eh_frame` sections for CFI merging.
- [ ] Handle `SHN_COMMON` symbols.
- [ ] Handle `STT_TLS` symbols.
- [ ] Track per-section alignment requirements.

### 2b. Static Archives (`.a`)
- [ ] Parse `!<arch>\n` magic and member headers.
- [ ] Read symbol table (`/` or `__.SYMDEF SORTED`) for demand extraction.
- [ ] Extract only members that resolve undefined symbols (lazy).
- [ ] With `--whole-archive`: extract all members.
- [ ] Support `--start-group`/`--end-group` iterative resolution.
- [ ] Handle thin archives (`!<thin>\n`).
- [ ] Track member provenance for diagnostics.

### 2c. Shared Libraries (ET_DYN input)
- [ ] Parse `.dynsym`/`.dynstr` for symbol availability.
- [ ] Record `DT_SONAME` (or filename fallback) for `DT_NEEDED`.
- [ ] Apply `--as-needed` filtering (only emit DT_NEEDED if symbols used).
- [ ] Resolve weak/global symbols from DSOs.
- [ ] Do not copy DSO section data into output.

### 2d. Linker Scripts
- [ ] Parse `SECTIONS { ... }` with section placement rules.
- [ ] Parse `MEMORY { ... }` for named memory regions.
- [ ] Parse `PHDRS { ... }` for explicit segment assignment.
- [ ] Support `/DISCARD/` section.
- [ ] Evaluate expressions: `ADDR()`, `SIZEOF()`, `ALIGN()`, `NEXT()`, `LOADADDR()`, arithmetic.
- [ ] Symbol assignment: `sym = expr;`, `PROVIDE(sym = expr)`, `PROVIDE_HIDDEN(sym = expr)`.
- [ ] `KEEP()` for GC-resistant sections.
- [ ] `SORT_BY_NAME()`, `SORT_BY_ALIGNMENT()`, `SORT_BY_INIT_PRIORITY()`.
- [ ] `INPUT()`, `GROUP()`, `OUTPUT()`, `SEARCH_DIR()`, `STARTUP()`.
- [ ] `OUTPUT_FORMAT()`, `OUTPUT_ARCH()`, `ENTRY()`.
- [ ] `INCLUDE file` nested script inclusion.
- [ ] `ASSERT(expr, message)` link-time assertions.
- [ ] `INSERT BEFORE/AFTER section` for augmenting default script.
- [ ] Default built-in linker scripts per target.
- [ ] `-T` override: replaces default, not augments.
- [ ] Error diagnostics with line/column for parse errors.

---

## 3. Symbol Resolution

- [ ] Build global symbol table from all inputs.
- [ ] **Binding precedence:** strong global > weak global > common > undefined.
- [ ] **Multiple definition:** error for conflicting strong definitions (unless `--allow-multiple-definition`).
- [ ] **Common symbols:** allocate in `.bss`, largest size/alignment wins.
- [ ] **Weak symbols:** resolve to strong if available; remain weak if not.
- [ ] **Undefined symbols:** error for ET_EXEC/ET_DYN (unless `--unresolved-symbols=ignore-all`/`ignore-in-object-files`/`ignore-in-shared-libs`).
- [ ] **Visibility:** `STV_DEFAULT`, `STV_HIDDEN`, `STV_PROTECTED`, `STV_INTERNAL` — most restrictive wins.
- [ ] **Archive extraction:** pull member only when it resolves an undefined; iterate with `--start-group`.
- [ ] **Symbol interposition:** default visibility symbols can be interposed by earlier DSOs.
- [ ] **Version definitions:** parse `--version-script` into version nodes; assign versions to symbols.
- [ ] **Version references:** record needed version info from input DSOs.
- [ ] **Symbol aliases:** track via `.symver` directives in objects.
- [ ] Built-in symbols: `_start`, `__bss_start`, `_end`, `_etext`, `_edata`, `__executable_start`, `__dso_handle`.
- [ ] `--defsym sym=value` command-line symbol definitions.
- [ ] `--undefined sym` force symbol as undefined (trigger archive extraction).
- [ ] `--export-dynamic` / `--export-dynamic-symbol=sym/glob` control dynamic symbol export.
- [ ] `--dynamic-list=file` / `--dynamic-list-data` / `--dynamic-list-cpp-new` / `--dynamic-list-cpp-typeinfo`.
- [ ] Deterministic symbol resolution order (input order).
- [ ] Diagnostic: "undefined reference to `sym`" with source file/section/offset context.

---

## 4. Relocation Processing

### 4a. i386 Relocations
- [ ] `R_386_NONE`, `R_386_32`, `R_386_PC32`
- [ ] `R_386_GOT32`, `R_386_PLT32`, `R_386_COPY`, `R_386_GLOB_DAT`, `R_386_JMP_SLOT`, `R_386_RELATIVE`
- [ ] `R_386_GOTOFF`, `R_386_GOTPC`, `R_386_GOT32X`
- [ ] `R_386_TLS_GD`, `R_386_TLS_LDM`, `R_386_TLS_LDO_32`, `R_386_TLS_IE`, `R_386_TLS_LE`, `R_386_TLS_TPOFF`, `R_386_TLS_GOTIE`, `R_386_TLS_LE_32`, `R_386_TLS_DTPMOD32`, `R_386_TLS_DTPOFF32`
- [ ] `R_386_16`, `R_386_PC16`, `R_386_8`, `R_386_PC8`
- [ ] `R_386_SIZE32`

### 4b. x86-64 Relocations
- [ ] `R_X86_64_NONE`, `R_X86_64_64`, `R_X86_64_PC32`, `R_X86_64_GOT32`, `R_X86_64_PLT32`
- [ ] `R_X86_64_COPY`, `R_X86_64_GLOB_DAT`, `R_X86_64_JUMP_SLOT`, `R_X86_64_RELATIVE`
- [ ] `R_X86_64_GOTPCREL`, `R_X86_64_32`, `R_X86_64_32S`, `R_X86_64_16`, `R_X86_64_PC16`, `R_X86_64_8`, `R_X86_64_PC8`
- [ ] `R_X86_64_DTPMOD64`, `R_X86_64_DTPOFF64`, `R_X86_64_TPOFF64`, `R_X86_64_TLSGD`, `R_X86_64_TLSLD`, `R_X86_64_DTPOFF32`, `R_X86_64_GOTTPOFF`, `R_X86_64_TPOFF32`
- [ ] `R_X86_64_PC64`, `R_X86_64_GOTOFF64`, `R_X86_64_GOTPC32`
- [ ] `R_X86_64_SIZE32`, `R_X86_64_SIZE64`
- [ ] `R_X86_64_GOTPCRELX`, `R_X86_64_REX_GOTPCRELX`
- [ ] `R_X86_64_IRELATIVE`

### 4c. ARMv7 Relocations
- [ ] `R_ARM_NONE`, `R_ARM_ABS32`, `R_ARM_REL32`, `R_ARM_PC24`
- [ ] `R_ARM_CALL`, `R_ARM_JUMP24`, `R_ARM_THM_CALL`, `R_ARM_THM_JUMP24`, `R_ARM_THM_JUMP11`, `R_ARM_THM_JUMP8`
- [ ] `R_ARM_MOVW_ABS_NC`, `R_ARM_MOVT_ABS`, `R_ARM_THM_MOVW_ABS_NC`, `R_ARM_THM_MOVT_ABS`
- [ ] `R_ARM_MOVW_PREL_NC`, `R_ARM_MOVT_PREL`, `R_ARM_THM_MOVW_PREL_NC`, `R_ARM_THM_MOVT_PREL`
- [ ] `R_ARM_GOT_BREL`, `R_ARM_PLT32`, `R_ARM_GOTOFF32`, `R_ARM_GOTPC`, `R_ARM_GOT32`
- [ ] `R_ARM_COPY`, `R_ARM_GLOB_DAT`, `R_ARM_JUMP_SLOT`, `R_ARM_RELATIVE`
- [ ] `R_ARM_TLS_GD32`, `R_ARM_TLS_LDM32`, `R_ARM_TLS_LDO32`, `R_ARM_TLS_IE32`, `R_ARM_TLS_LE32`, `R_ARM_TLS_DTPMOD32`, `R_ARM_TLS_DTPOFF32`, `R_ARM_TLS_TPOFF32`
- [ ] `R_ARM_PREL31`, `R_ARM_TARGET1`, `R_ARM_TARGET2`, `R_ARM_V4BX`
- [ ] ARM/Thumb interwork veneer generation.

### 4d. AArch64 Relocations
- [ ] `R_AARCH64_NONE`, `R_AARCH64_ABS64`, `R_AARCH64_ABS32`, `R_AARCH64_ABS16`
- [ ] `R_AARCH64_PREL64`, `R_AARCH64_PREL32`, `R_AARCH64_PREL16`
- [ ] `R_AARCH64_ADR_PREL_PG_HI21`, `R_AARCH64_ADR_PREL_LO21`
- [ ] `R_AARCH64_ADD_ABS_LO12_NC`
- [ ] `R_AARCH64_LDST8_ABS_LO12_NC`, `R_AARCH64_LDST16_ABS_LO12_NC`, `R_AARCH64_LDST32_ABS_LO12_NC`, `R_AARCH64_LDST64_ABS_LO12_NC`, `R_AARCH64_LDST128_ABS_LO12_NC`
- [ ] `R_AARCH64_MOVW_UABS_G0/G1/G2/G3{_NC}`, `R_AARCH64_MOVW_SABS_G0/G1/G2`
- [ ] `R_AARCH64_JUMP26`, `R_AARCH64_CALL26`, `R_AARCH64_CONDBR19`, `R_AARCH64_TSTBR14`
- [ ] `R_AARCH64_COPY`, `R_AARCH64_GLOB_DAT`, `R_AARCH64_JUMP_SLOT`, `R_AARCH64_RELATIVE`
- [ ] `R_AARCH64_ADR_GOT_PAGE`, `R_AARCH64_LD64_GOT_LO12_NC`, `R_AARCH64_GOT_LD_PREL19`
- [ ] `R_AARCH64_TLSGD_ADR_PAGE21`, `R_AARCH64_TLSGD_ADD_LO12_NC`
- [ ] `R_AARCH64_TLSLE_ADD_TPREL_HI12`, `R_AARCH64_TLSLE_ADD_TPREL_LO12{_NC}`
- [ ] `R_AARCH64_TLSLE_MOVW_TPREL_G0/G1/G2{_NC}`
- [ ] `R_AARCH64_TLSIE_ADR_GOTTPREL_PAGE21`, `R_AARCH64_TLSIE_LD64_GOTTPREL_LO12_NC`
- [ ] `R_AARCH64_TLSDESC_ADR_PAGE21`, `R_AARCH64_TLSDESC_LD64_LO12`, `R_AARCH64_TLSDESC_ADD_LO12`, `R_AARCH64_TLSDESC_CALL`
- [ ] `R_AARCH64_IRELATIVE`
- [ ] AArch64 range-extension thunk generation (veneer stubs for out-of-range branches).

### 4e. Relocation Engine
- [ ] Architecture backend abstraction: `reloc_apply(arch, type, place, sym_value, addend)`.
- [ ] Overflow detection with precise diagnostics (arch, reloc type, section, offset, symbol).
- [ ] Relocation against discarded sections: error or zero based on policy.
- [ ] Relocation ordering for `DT_RELR` compact format (optional).

---

## 5. Section Merging and Layout

- [ ] Collect input sections, merge by name/type/flags.
- [ ] Alignment: propagate maximum alignment from inputs.
- [ ] `.bss` (SHF_ALLOC|SHF_WRITE, SHT_NOBITS): account for size, no file data.
- [ ] `SHF_MERGE`: merge identical constant pool entries.
- [ ] `SHF_STRINGS`: merge NUL-terminated strings (tail merge).
- [ ] COMDAT / section groups: keep first, discard duplicates.
- [ ] Orphan section placement: heuristic by flags (alloc+exec→near .text, alloc+write→near .data, etc).
- [ ] Default output ordering: `.interp`, `.note.*`, `.hash`, `.gnu.hash`, `.dynsym`, `.dynstr`, `.gnu.version*`, `.rel*/.rela*`, `.init`, `.plt`, `.text`, `.fini`, `.rodata`, `.eh_frame_hdr`, `.eh_frame`, `.init_array`, `.fini_array`, `.data.rel.ro`, `.dynamic`, `.got`, `.got.plt`, `.data`, `.bss`.
- [ ] `.eh_frame` CIE deduplication and FDE merging.
- [ ] `.eh_frame_hdr` generation (sorted binary search table).
- [ ] `--gc-sections`: mark reachable from entry + KEEP, discard unreachable.
- [ ] `--icf=safe`/`all` (identical code folding): merge identical sections.
- [ ] Deterministic final layout with predictable padding.

---

## 6. Segment Construction and Program Headers

- [ ] `PT_LOAD` segments: group sections by permission (RX, R, RW), separate NOBITS tail.
- [ ] Page alignment: `MAXPAGESIZE` (default 4096 i386, 65536 AArch64) for segment start alignment.
- [ ] File offset ≡ vaddr (mod MAXPAGESIZE) for each PT_LOAD.
- [ ] `PT_PHDR`: self-referencing program header (for ET_EXEC/ET_DYN with phdr).
- [ ] `PT_INTERP`: path from `--dynamic-linker` or default per target.
- [ ] `PT_DYNAMIC`: points to `.dynamic` section.
- [ ] `PT_TLS`: covers `.tdata`+`.tbss` with correct memsz/filesz.
- [ ] `PT_NOTE`: covers `.note.*` sections.
- [ ] `PT_GNU_EH_FRAME`: points to `.eh_frame_hdr`.
- [ ] `PT_GNU_STACK`: executable stack flag.
- [ ] `PT_GNU_RELRO`: read-only after relocation segment (`.data.rel.ro`, `.dynamic`, `.got`, partial `.got.plt`).
- [ ] `PT_GNU_PROPERTY`: points to `.note.gnu.property`.
- [ ] Entry point: `-e` override, or `_start`, or `start`, or first `.text` address.
- [ ] Segment ordering: PHDR, INTERP, LOAD(RX), LOAD(R), LOAD(RW), DYNAMIC, NOTE, TLS, EH_FRAME, STACK, RELRO.
- [ ] W^X enforcement: no segment both writable and executable (warn on violation with `-z text`).

---

## 7. Dynamic Linking Artifacts

### 7a. Dynamic Section (`.dynamic`)
- [ ] `DT_NEEDED` for each input DSO (respecting `--as-needed`).
- [ ] `DT_SONAME` if `-soname` specified.
- [ ] `DT_RPATH`/`DT_RUNPATH` from `-rpath`.
- [ ] `DT_HASH` / `DT_GNU_HASH` per `--hash-style`.
- [ ] `DT_STRTAB`, `DT_STRSZ`, `DT_SYMTAB`, `DT_SYMENT`.
- [ ] `DT_REL`/`DT_RELA`/`DT_RELSZ`/`DT_RELASZ`/`DT_RELENT`/`DT_RELAENT`.
- [ ] `DT_JMPREL`, `DT_PLTRELSZ`, `DT_PLTREL`, `DT_PLTGOT`.
- [ ] `DT_INIT`, `DT_FINI`, `DT_INIT_ARRAY`, `DT_FINI_ARRAY`, `DT_INIT_ARRAYSZ`, `DT_FINI_ARRAYSZ`.
- [ ] `DT_FLAGS` (`DF_BIND_NOW`, `DF_ORIGIN`, `DF_SYMBOLIC`, `DF_TEXTREL`, `DF_STATIC_TLS`).
- [ ] `DT_FLAGS_1` (`DF_1_NOW`, `DF_1_PIE`, `DF_1_NODELETE`, `DF_1_NODUMP`, `DF_1_ORIGIN`).
- [ ] `DT_VERNEED`, `DT_VERNEEDNUM`, `DT_VERDEF`, `DT_VERDEFNUM`, `DT_VERSYM`.
- [ ] `DT_DEBUG` (for ET_EXEC).
- [ ] `DT_TEXTREL` warning.
- [ ] `DT_NULL` terminator.

### 7b. PLT/GOT Synthesis
- [ ] **i386:** `.plt` entries (push GOT slot, jmp to resolver), `.got.plt` lazy binding stubs. PLT0 = push GOT[1], jmp GOT[2].
- [ ] **x86-64:** `.plt` entries (jmp *GOT[n](%rip), push index, jmp PLT0). PLT0 = push GOT[1], jmp *GOT[2].
- [ ] **ARMv7:** `.plt` entries (add ip, pc, #page; ldr ip, [ip, #offset]; bx ip). Thumb PLT variants.
- [ ] **AArch64:** `.plt` entries (adrp x16, GOT page; ldr x17, [x16, #lo12]; br x17).
- [ ] `.got` section for `R_*_GLOB_DAT`.
- [ ] `.got.plt` section for lazy PLT resolution.
- [ ] IFUNC: `R_*_IRELATIVE` support, STT_GNU_IFUNC resolution.
- [ ] Copy relocations for data imported from DSOs.

### 7c. Hash Tables
- [ ] SYSV hash: `DT_HASH`, `nbuckets`+`nchain`+`buckets[]+chain[]`.
- [ ] GNU hash: `DT_GNU_HASH`, `nbuckets`+`symndx`+`maskwords`+`shift2`+bloom filter+buckets+chains.
- [ ] `--hash-style=both` emits both tables.

### 7d. Symbol Versioning
- [ ] `.gnu.version` (per-dynsym version index).
- [ ] `.gnu.version_d` (version definitions from `--version-script`).
- [ ] `.gnu.version_r` (version requirements from input DSOs).

---

## 8. TLS Support

- [ ] **Local Exec (LE):** direct TP-relative offset, static executables.
- [ ] **Initial Exec (IE):** GOT-indirect TP-relative, main executable DSO-aware.
- [ ] **General Dynamic (GD):** full `__tls_get_addr` call, shared libraries.
- [ ] **Local Dynamic (LD):** optimized GD for module-local TLS.
- [ ] TLS-to-LE / TLS-to-IE relaxation when linking statically or at link time.
- [ ] PT_TLS segment with correct template image, alignment, offset.
- [ ] DTV (Dynamic Thread Vector) slot allocation semantics.
- [ ] Per-arch TLS layout: variant I (AArch64, x86-64: TP before TLS) vs variant II (i386, ARM: TP after TLS).

---

## 9. Linker Relaxation and Optimization

- [ ] **x86-64 GOT relaxation:** `GOTPCRELX`/`REX_GOTPCRELX` → direct LEA when symbol is non-preemptible.
- [ ] **AArch64 relaxation:** ADRP+LDR → ADRP+ADD for local symbols; ADRP+ADD pairs that can be relaxed when within range.
- [ ] **ARM relaxation:** Thumb BL range extension with veneers.
- [ ] **TLS relaxation:** GD→IE→LE model downgrade based on output type and symbol scope.
- [ ] **String merging:** `.rodata.str1.*` tail-merge optimization at `-O2`.
- [ ] **Identical code folding:** merge sections with identical content+relocations at `--icf`.

---

## 10. Build-ID and Notes

- [ ] `--build-id=sha1` (default): SHA-1 hash of output contents.
- [ ] `--build-id=md5`/`sha256`/`uuid`/`none`/`0xHEX`.
- [ ] `.note.gnu.build-id` section with NT_GNU_BUILD_ID type.
- [ ] `.note.gnu.property` passthrough and merging (x86 ISA level, BTI, PAC).
- [ ] `.note.ABI-tag` passthrough.
- [ ] `.comment` section merging.

---

## 11. Diagnostics, Safety, and Reproducibility

- [ ] Diagnostic format: `ld: error: file.o:(.text+0x1a): undefined reference to 'sym'`.
- [ ] Context: input file, archive member, section, offset, symbol, relocation type.
- [ ] `-Map file` output: symbols, sections, input→output mapping, discarded sections.
- [ ] `--trace` / `-y` symbol tracing.
- [ ] Bounds checking on all input offsets/sizes (reject malformed ELF).
- [ ] Deterministic output: same inputs+options → byte-identical output.
- [ ] Stable diagnostic ordering.
- [ ] OOM handling: fail gracefully with message.
- [ ] Fuzz hardening: ELF parser, linker script parser, archive parser.

---

## 12. Testing

### 12a. Unit Tests
- [ ] Symbol resolution: strong/weak/common/undefined/version interactions.
- [ ] Relocation overflow: edge cases for each reloc type per arch.
- [ ] Section merging: COMDAT, SHF_MERGE, SHF_STRINGS.
- [ ] Expression evaluator: ALIGN, SIZEOF, nested arithmetic.
- [ ] TLS layout: each model per arch.

### 12b. Integration Tests
- [ ] `cc -c` + `ld` → running hello world (i386, x86-64, ARMv7, AArch64).
- [ ] Static archive: extraction by demand, `--whole-archive`, `--start-group`.
- [ ] Shared library: `ld -shared`, executable with `DT_NEEDED`, dynamic load (`dlopen`).
- [ ] PIE: position-independent executable with ASLR.
- [ ] `-r` partial link: output ET_REL, link again to final executable.
- [ ] Linker script: custom layout, MEMORY regions, ASSERT.
- [ ] `--gc-sections`: unreachable code removed, KEEP preserved.
- [ ] TLS: all 4 models for each arch.
- [ ] Build-id: verify hash present and correct.
- [ ] Determinism: two identical builds produce identical output.
- [ ] Large link: 10,000+ objects, 500,000+ symbols stress test.

### 12c. Compatibility Tests
- [ ] Substrate `ld` output loadable by GNU `ld.so`.
- [ ] Substrate `ld` output loadable by Substrate `ld.so`.
- [ ] GNU `ld` output loadable by Substrate `ld.so`.
- [ ] Substrate `as` + Substrate `ld` full toolchain path.
- [ ] `readelf -a` structural validation on all outputs.

### 12d. Fuzz Tests
- [ ] ELF object parser harness.
- [ ] Archive parser harness.
- [ ] Linker script parser harness.
- [ ] Crash-free guarantee on arbitrary input.

---

## 13. Build System and Integration

- [ ] Recursive Makefile, `NATIVE_BUILD=1` for host testing.
- [ ] `install` to `$(DESTDIR)/usr/bin/ld`.
- [ ] `libelfobj.a` dependency (auto-build via recursive make).
- [ ] `ld.1` man page: all options, linker script language, per-arch notes.
- [ ] Default linker script files installed to `$(DESTDIR)/usr/lib/ldscripts/`.
- [ ] Driver integration with `cc` (compiler driver calls `ld` with correct flags).
- [ ] Arch-specific symlinks or multi-call: `ld.bfd`-compatible invocation.

---

## 14. Architecture Backend Abstraction

- [ ] `struct ld_target` with function pointers: `apply_reloc`, `create_plt_entry`, `create_got_entry`, `relax`, `create_tls_entries`, `veneer_needed`, `create_veneer`.
- [ ] `ld_target_i386`, `ld_target_x86_64`, `ld_target_arm`, `ld_target_aarch64` implementations.
- [ ] Runtime target selection from CLI or input inference.
- [ ] Per-target default linker script, page sizes, entry point name.
- [ ] Per-target PLT/GOT layout and stub code templates.
