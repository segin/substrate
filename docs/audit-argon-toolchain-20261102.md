# Argon Toolchain Audit & Actionable Tasklist (`usr.lib/elfobj`, `usr.bin/ld`, `usr.bin/nm`, `usr.bin/ar`, `usr.bin/addr2line`, `usr.bin/cxxfilt`)

**Audit Date:** 2026-11-02  
**Scope:** [`include/elfobj.h`](file:///home/segin/substrate/include/elfobj.h), [`usr.lib/elfobj/`](file:///home/segin/substrate/usr.lib/elfobj), [`usr.bin/ld/`](file:///home/segin/substrate/usr.bin/ld), [`usr.bin/nm/`](file:///home/segin/substrate/usr.bin/nm), [`usr.bin/ar/`](file:///home/segin/substrate/usr.bin/ar), [`usr.bin/addr2line/`](file:///home/segin/substrate/usr.bin/addr2line), [`usr.bin/cxxfilt/`](file:///home/segin/substrate/usr.bin/cxxfilt), [`usr.man/man1/ld.1`](file:///home/segin/substrate/usr.man/man1/ld.1), [`usr.man/man1/ar.1`](file:///home/segin/substrate/usr.man/man1/ar.1), [`usr.man/man3/elfobj.3`](file:///home/segin/substrate/usr.man/man3/elfobj.3)  
**Requirement Syntax:** INCOSE Systems Engineering & EARS (Easy Approach to Requirements Syntax)  
**Execution Rule:** Complete one checkbox at a time; each completed item shall include unit/regression tests and documentation updates where applicable.

---

## EARS Pattern Legend

- **Ubiquitous:** `The <system/component> shall <action>.`
- **Event-Driven:** `WHEN <trigger>, the <system/component> shall <action>.`
- **State-Driven:** `WHILE <state>, the <system/component> shall <action>.`
- **Unwanted Behavior:** `IF <condition>, THEN the <system/component> shall <action>.`
- **Optional Feature:** `WHERE <feature is enabled>, the <system/component> shall <action>.`
- **Complex:** Combinations of `WHERE` / `WHILE` / `WHEN` / `IF ... THEN`.

---

## 1. Critical & High-Severity Linker Correctness (`usr.bin/ld`)

### 1.1 Garbage Collection (`--gc-sections`) & `.eh_frame` Relocations
- [ ] **[REQ-LD-GC-001]** **WHERE** `--gc-sections` is enabled, **WHEN** [`merge_relocations()`](file:///home/segin/substrate/usr.lib/elfobj/src/elf_link.c#L2182-L2222) processes a relocation in `.eh_frame` (or any non-code unwind/metadata section) that references an anonymous `STT_SECTION` symbol belonging to a garbage-collected input section (`sym_map[i][old_sym->index] == NULL`), **THEN** `libelfobj` and `ld` shall neutralize the relocation (e.g., retarget to `R_*_NONE` or omit the FDE relocation) rather than passing `new_sym = NULL` to [`elf_add_relocation()`](file:///home/segin/substrate/usr.lib/elfobj/src/elf_sections.c#L661-L665) and failing the link with `ELF_ERR_PARAM`.
  - **Evidence:** [`ld_gc.c:58-65`](file:///home/segin/substrate/usr.bin/ld/ld_gc.c#L58-L65), [`elf_link.c:2198-2220`](file:///home/segin/substrate/usr.lib/elfobj/src/elf_link.c#L2198-L2220), [`elf_sections.c:663`](file:///home/segin/substrate/usr.lib/elfobj/src/elf_sections.c#L663).
  - **Verification:** Link a multi-function C/C++ object compiled with `-ffunction-sections -fasynchronous-unwind-tables` using `--gc-sections` and verify the unused function is removed and the link succeeds.

- [ ] **[REQ-LD-GC-002]** **WHERE** `--gc-sections` is enabled, **WHEN** [`merge_relocations()`](file:///home/segin/substrate/usr.lib/elfobj/src/elf_link.c#L2204-L2214) encounters a relocation in `.eh_frame` targeting a named symbol whose defining input section was discarded by [`gc_keep_input_section()`](file:///home/segin/substrate/usr.bin/ld/ld_gc.c#L278-L292), **THEN** `libelfobj` and `ld` shall not synthesize an `SHN_UNDEF` symbol in the output object that triggers an undefined-symbol error in [`check_unresolved_symbols()`](file:///home/segin/substrate/usr.bin/ld/ld_dso.c#L967-L1035) or [`apply_all_relocations()`](file:///home/segin/substrate/usr.bin/ld/ld_reloc.c#L1196-L1219).
  - **Evidence:** [`elf_link.c:2204-2214`](file:///home/segin/substrate/usr.lib/elfobj/src/elf_link.c#L2204-L2214), [`ld_reloc.c:1196-1219`](file:///home/segin/substrate/usr.bin/ld/ld_reloc.c#L1196-L1219).
  - **Verification:** Link an object where `.eh_frame` FDEs reference named global/local function symbols in discarded `.text.*` sections with `--gc-sections` and verify zero unresolved-symbol errors.

- [ ] **[REQ-LD-GC-003]** **IF** `shndx == 0` is passed to [`gc_section_of()`](file:///home/segin/substrate/usr.bin/ld/ld_gc.c#L92-L96) on an in-memory `elfobj_t` whose first section is not `SHT_NULL`, **THEN** [`gc_section_of()`](file:///home/segin/substrate/usr.bin/ld/ld_gc.c#L92-L96) shall return an out-of-range sentinel without unsigned integer underflow before callers index `st->live[obj]`.
  - **Evidence:** [`ld_gc.c:92-113`](file:///home/segin/substrate/usr.bin/ld/ld_gc.c#L92-L113).
  - **Verification:** Unit test [`gc_collect_input_sections()`](file:///home/segin/substrate/usr.bin/ld/ld_gc.c#L294-L409) with synthetic `elf_create()` input objects containing `SHN_UNDEF` symbols.

### 1.2 Identical Code Folding (`--icf`)
- [ ] **[REQ-LD-ICF-001]** **WHERE** `--icf=safe` or `--icf=all` is enabled, **WHEN** `ld` folds identical sections, `ld` shall perform candidate comparison and folding on input sections (prior to `.text.*`/`.rodata.*`/`.data.*` output-section coalescing in [`elf_link_plan_link()`](file:///home/segin/substrate/usr.lib/elfobj/src/elf_link.c#L2446-L2515)) rather than invoking [`apply_identical_code_folding()`](file:///home/segin/substrate/usr.bin/ld/ld_gc.c#L547-L572) after input sections have already been concatenated into `.text`.
  - **Evidence:** [`ld.c:394-447`](file:///home/segin/substrate/usr.bin/ld/ld.c#L394-L447), [`ld_gc.c:547-572`](file:///home/segin/substrate/usr.bin/ld/ld_gc.c#L547-L572).
  - **Verification:** Link two `.text.fn1` and `.text.fn2` sections with identical bodies into an executable with `--icf=all` and verify `fn1` and `fn2` resolve to the same virtual address.

- [ ] **[REQ-LD-ICF-002]** **WHERE** `--icf` is enabled, **WHEN** [`sections_reloc_signature_equal()`](file:///home/segin/substrate/usr.bin/ld/ld_gc.c#L446-L486) compares relocations of two candidate sections, **IF** the relocations target `STB_LOCAL` or `STT_SECTION` symbols, **THEN** [`sections_reloc_signature_equal()`](file:///home/segin/substrate/usr.bin/ld/ld_gc.c#L446-L486) shall verify that the target symbols resolve to the exact same section and offset (or recursively identical target section) rather than matching solely on `strcmp(na, nb) == 0`.
  - **Evidence:** [`ld_gc.c:474-484`](file:///home/segin/substrate/usr.bin/ld/ld_gc.c#L474-L484).
  - **Verification:** Link two translation units each containing a `static int x` with different initializers and identical getter functions `get_a()` / `get_b()`; verify `--icf=all` does not fold `get_a()` and `get_b()` together.

- [ ] **[REQ-LD-ICF-003]** **WHERE** `--icf=safe` (`icf_mode == 1`) is enabled, **WHEN** evaluating candidate functions for folding, `ld` shall exclude sections whose symbols have address-taking relocations (non-call relocations) so C/C++ function pointer inequality invariants are preserved.
  - **Evidence:** [`ld_gc.c:420-444`](file:///home/segin/substrate/usr.bin/ld/ld_gc.c#L420-L444).
  - **Verification:** Link two identical functions whose addresses are compared (`&f1 == &f2`) under `--icf=safe` and verify they remain distinct.

- [ ] **[REQ-LD-ICF-004]** **WHEN** [`icf_fold_section()`](file:///home/segin/substrate/usr.bin/ld/ld_gc.c#L515-L545) updates symbol section indices, it shall store `dupe_shndx` and `leader_shndx` as full `uint32_t` values without `uint16_t` truncation and shall adjust `leader_shndx` if `leader_idx > dupe_idx` when [`elf_remove_section()`](file:///home/segin/substrate/usr.lib/elfobj/src/elf_sections.c#L430-L475) shifts section indices.
  - **Evidence:** [`ld_gc.c:528-544`](file:///home/segin/substrate/usr.bin/ld/ld_gc.c#L528-L544).
  - **Verification:** Code inspection and unit test with `SHN_LORESERVE`+ section counts.

### 1.3 Shared Object (DSO) Versioned Symbol Resolution
- [ ] **[REQ-LD-DSO-001]** **WHEN** [`dso_find_definition()`](file:///home/segin/substrate/usr.bin/ld/ld_dynamic.c#L620-L650) searches DSO inputs for a symbol definition, it shall strip `@version` / `@@version` suffixes via [`split_symbol_version()`](file:///home/segin/substrate/usr.bin/ld/ld_dso.c#L510-L538) (respecting default `@@` vs non-default `@` version matching rules) so that unstripped shared objects (where [`elf_open()`](file:///home/segin/substrate/usr.lib/elfobj/src/elf_read.c#L965-L974) loads `SHT_SYMTAB` containing `"sym@@VER"` names) match unversioned references `"sym"`.
  - **Evidence:** [`ld_dynamic.c:620-650`](file:///home/segin/substrate/usr.bin/ld/ld_dynamic.c#L620-L650), [`elf_read.c:965-974`](file:///home/segin/substrate/usr.lib/elfobj/src/elf_read.c#L965-L974).
  - **Verification:** Link an executable against an unstripped `.so` that defines versioned function and `STT_OBJECT` symbols (`foo@@VER_1`, `var@@VER_1`); verify [`type_imports_as_defined()`](file:///home/segin/substrate/usr.bin/ld/ld_dynamic.c#L663-L685) and [`plan_copy_relocs()`](file:///home/segin/substrate/usr.bin/ld/ld_dynamic.c#L707-L813) properly type the function and emit `R_*_COPY` for `var`.

- [ ] **[REQ-LD-DSO-002]** **WHEN** [`plan_copy_relocs()`](file:///home/segin/substrate/usr.bin/ld/ld_dynamic.c#L707-L813) pairs a copied DSO `STT_OBJECT` symbol with an alias at the same address and size in the DSO, it shall strip `@version` / `@@version` suffixes from the alias name (`aname`) before defining the alias in `.dynbss` and copying version metadata.
  - **Evidence:** [`ld_dynamic.c:785-810`](file:///home/segin/substrate/usr.bin/ld/ld_dynamic.c#L785-L810).
  - **Verification:** Link an executable referencing a weak alias of a versioned `STT_OBJECT` in an unstripped `.so` and verify `.dynsym` contains the bare symbol name with its `.gnu.version` entry.

---

## 2. Critical & High-Severity `libelfobj` Bugs (`usr.lib/elfobj` & `include/elfobj.h`)

### 2.1 Object Serialization & Lifecycle (`elf_write.c`, `elf_util.c`)
- [ ] **[REQ-ELF-WR-001]** **WHEN** [`elf_write_file()`](file:///home/segin/substrate/usr.lib/elfobj/src/elf_write.c#L2080-L2105) serializes a modified object (`obj->dirty != 0`) that was originally opened from a file or memory buffer (`obj->image != NULL`), `libelfobj` shall invalidate or replace `obj->image` (or track that `obj->image` is stale) so that a subsequent call to [`elf_write_file()`](file:///home/segin/substrate/usr.lib/elfobj/src/elf_write.c#L2080-L2105) on the same `elfobj_t` writes the modified object rather than the original unmodified `obj->image`.
  - **Evidence:** [`elf_write.c:2082-2104`](file:///home/segin/substrate/usr.lib/elfobj/src/elf_write.c#L2082-L2104), [`elf_util.c:866-867`](file:///home/segin/substrate/usr.lib/elfobj/src/elf_util.c#L866-L867).
  - **Verification:** Open an ELF file via `elf_open()`, mutate a section or symbol, call `elf_write_file(obj, path1)` followed by `elf_write_file(obj, path2)`, and verify `path1` and `path2` are byte-for-byte identical.

- [ ] **[REQ-ELF-WR-002]** **WHEN** [`elf__write_to_buffer()`](file:///home/segin/substrate/usr.lib/elfobj/src/elf_write.c#L1427-L1452) auto-generates program headers for an `ET_EXEC` or `ET_DYN` object with `obj->segment_count == 0 && obj->phdr_count == 0`, the pre-calculated `phnum` and the emitted program header table at [`elf_write.c:1975-2014`](file:///home/segin/substrate/usr.lib/elfobj/src/elf_write.c#L1975-L2014) shall coalesce multiple `SHF_TLS` sections (`.tdata` and `.tbss`) into a single `PT_TLS` entry and shall include `PT_ARM_EXIDX` in `phnum` when `.ARM.exidx` (`SHT_ARM_EXIDX`) is present.
  - **Evidence:** [`elf_write.c:1427-1452`](file:///home/segin/substrate/usr.lib/elfobj/src/elf_write.c#L1427-L1452), [`elf_write.c:1975-2014`](file:///home/segin/substrate/usr.lib/elfobj/src/elf_write.c#L1975-L2014).
  - **Verification:** Create an `ET_DYN` object with both `.tdata` and `.tbss` (and an ARM object with `.ARM.exidx`) without explicit segments, serialize via `elf_write_file()`, and validate with `elf_validate()`.

- [ ] **[REQ-ELF-WR-003]** **IF** an allocation or `out_push()` error occurs in [`elf__write_to_buffer()`](file:///home/segin/substrate/usr.lib/elfobj/src/elf_write.c#L1048-L1225) after [`build_symtab()`](file:///home/segin/substrate/usr.lib/elfobj/src/elf_write.c#L1048) or [`build_dyn_tables()`](file:///home/segin/substrate/usr.lib/elfobj/src/elf_write.c#L1207) succeeds, **THEN** [`elf__write_to_buffer()`](file:///home/segin/substrate/usr.lib/elfobj/src/elf_write.c#L1048-L1225) shall free `symtab_data`, `xindex_data`, and `dynstr_data` before returning if they have not yet transferred ownership to `secs`.
  - **Evidence:** [`elf_write.c:1048-1115`](file:///home/segin/substrate/usr.lib/elfobj/src/elf_write.c#L1048-L1115), [`elf_write.c:1207-1225`](file:///home/segin/substrate/usr.lib/elfobj/src/elf_write.c#L1207-L1225), [`elf_write.c:2065-2077`](file:///home/segin/substrate/usr.lib/elfobj/src/elf_write.c#L2065-L2077).
  - **Verification:** Fault-injection OOM test in `elf__write_to_buffer()` under AddressSanitizer/LeakSanitizer.

- [ ] **[REQ-ELF-WR-004]** The duplicate assignment statements at [`elf_write.c:868-870`](file:///home/segin/substrate/usr.lib/elfobj/src/elf_write.c#L868-L870) (duplicating lines 864–866) shall be removed.
  - **Evidence:** [`elf_write.c:864-870`](file:///home/segin/substrate/usr.lib/elfobj/src/elf_write.c#L864-L870).
  - **Verification:** Clean build of `usr.lib/elfobj`.

### 2.2 Relocation Backends & Public Header Constants (`elf_reloc.c`, `include/elfobj.h`)
- [ ] **[REQ-ELF-REL-001]** **WHEN** applying 32-bit absolute relocations (`R_ARM_ABS32`, `R_MIPS_32`, `R_RISCV_32`, `R_LARCH_32`, `R_68K_32`, `R_VAX_32`) in [`elf_reloc.c`](file:///home/segin/substrate/usr.lib/elfobj/src/elf_reloc.c), the backend `apply` functions shall accept values that fit in either signed 32-bit (`swide_in_signed_bits(v, 32)`) or unsigned 32-bit (`swide_in_unsigned_bits(v, 32)`) range (matching `R_386_32` at [`elf_reloc.c:144`](file:///home/segin/substrate/usr.lib/elfobj/src/elf_reloc.c#L144)), so that negative addends where `sym_value + addend < 0` do not spuriously fail with `ELF_ERR_RELOC`.
  - **Evidence:** [`elf_reloc.c:430-433`](file:///home/segin/substrate/usr.lib/elfobj/src/elf_reloc.c#L430-L433), [`elf_reloc.c:728-731`](file:///home/segin/substrate/usr.lib/elfobj/src/elf_reloc.c#L728-L731), [`elf_reloc.c:964-967`](file:///home/segin/substrate/usr.lib/elfobj/src/elf_reloc.c#L964-L967), [`elf_reloc.c:1218-1221`](file:///home/segin/substrate/usr.lib/elfobj/src/elf_reloc.c#L1218-L1221), [`elf_reloc.c:1425-1428`](file:///home/segin/substrate/usr.lib/elfobj/src/elf_reloc.c#L1425-L1428), [`elf_reloc.c:1562-1565`](file:///home/segin/substrate/usr.lib/elfobj/src/elf_reloc.c#L1562-L1565).
  - **Verification:** Unit test `elf_apply_relocation_value()` on all six architectures with `sym_value = 0, addend = -4` (`0xfffffffc`).

- [ ] **[REQ-ELF-REL-002]** **WHEN** applying `R_AARCH64_ABS16` in [`aarch64_apply()`](file:///home/segin/substrate/usr.lib/elfobj/src/elf_reloc.c#L2420-L2424), `libelfobj` shall accept values in either signed 16-bit or unsigned 16-bit range (`[-32768, 65535]`), matching the AArch64 ELF specification and `R_386_16` / `R_X86_64_16` / `R_ARM_ABS16`.
  - **Evidence:** [`elf_reloc.c:2420-2424`](file:///home/segin/substrate/usr.lib/elfobj/src/elf_reloc.c#L2420-L2424).
  - **Verification:** Unit test `R_AARCH64_ABS16` with `sym_value = 0xffff, addend = 0`.

- [ ] **[REQ-ELF-REL-003]** [`include/elfobj.h`](file:///home/segin/substrate/include/elfobj.h) shall define non-colliding, ABI-conformant relocation numbers for AArch64 128-bit TLS relocations and IA-64 TLS relocations:
  - `R_AARCH64_TLSLE_LDST128_TPREL_LO12` = `570` (currently `560`, colliding with `R_AARCH64_TLSDESC_LD_PREL19`)
  - `R_AARCH64_TLSLE_LDST128_TPREL_LO12_NC` = `571` (currently `561`, colliding with `R_AARCH64_TLSDESC_ADR_PREL21`)
  - `R_AARCH64_TLSLD_LDST128_DTPREL_LO12` = `572` (currently `539`, colliding with `R_AARCH64_TLSIE_MOVW_GOTTPREL_G1`)
  - `R_AARCH64_TLSLD_LDST128_DTPREL_LO12_NC` = `573` (currently `540`, colliding with `R_AARCH64_TLSIE_MOVW_GOTTPREL_G0_NC`)
  - `R_IA64_LTOFF_DTPREL22` = `0xab` (currently `0xb2`, colliding with `R_IA64_DTPREL22`)
  - **Evidence:** [`include/elfobj.h:539-540`](file:///home/segin/substrate/include/elfobj.h#L539-L540), [`include/elfobj.h:743-760`](file:///home/segin/substrate/include/elfobj.h#L743-L760), [`include/elfobj.h:1256-1257`](file:///home/segin/substrate/include/elfobj.h#L1256-L1257).
  - **Verification:** Compile-time uniqueness check (`switch` statement over all per-architecture `R_*` constants).

- [ ] **[REQ-ELF-REL-004]** [`x64_is_pc_relative()`](file:///home/segin/substrate/usr.lib/elfobj/src/elf_reloc.c#L287-L299) shall return `1` for `R_X86_64_GOTPC32`, `R_X86_64_GOTPC64`, `R_X86_64_GOTPCREL64`, `R_X86_64_GOTPCRELX`, `R_X86_64_REX_GOTPCRELX`, and `R_X86_64_GOTPC32_TLSDESC`.
  - **Evidence:** [`elf_reloc.c:287-326`](file:///home/segin/substrate/usr.lib/elfobj/src/elf_reloc.c#L287-L326).
  - **Verification:** Unit test `elf_reloc_is_pc_relative_for_machine(EM_X86_64, type)` for all GOTPC/GOTPCREL relocation types.

- [ ] **[REQ-ELF-REL-005]** **WHEN** [`elf_section_clear_relocations()`](file:///home/segin/substrate/usr.lib/elfobj/src/elf_reloc.c#L2865-L2876) or [`elf_reloc_retarget()`](file:///home/segin/substrate/usr.lib/elfobj/src/elf_reloc.c#L2940-L2952) is called on a finalized or read-only `elfobj_t`, it shall return `ELF_ERR_STATE`; **WHEN** called on a mutable `elfobj_t`, it shall set `obj->dirty = 1`.
  - **Evidence:** [`elf_reloc.c:2865-2876`](file:///home/segin/substrate/usr.lib/elfobj/src/elf_reloc.c#L2865-L2876), [`elf_reloc.c:2940-2952`](file:///home/segin/substrate/usr.lib/elfobj/src/elf_reloc.c#L2940-L2952).
  - **Verification:** Unit test mutating relocations before and after `elf_finalize()`.

### 2.3 Lazy Parsing Completeness (`ELFOBJ_OPEN_LAZY_PARSE`)
- [ ] **[REQ-ELF-LAZY-001]** **WHERE** an `elfobj_t` is opened with `ELFOBJ_OPEN_LAZY_PARSE` (or `ELFOBJ_LAZY_PARSE=1`), **WHEN** [`elf_remove_section()`](file:///home/segin/substrate/usr.lib/elfobj/src/elf_sections.c#L430-L475), [`elf_reorder_section()`](file:///home/segin/substrate/usr.lib/elfobj/src/elf_sections.c#L477-L561), [`merge_sections()`](file:///home/segin/substrate/usr.lib/elfobj/src/elf_link.c#L1929-L2020), [`merge_symbols()`](file:///home/segin/substrate/usr.lib/elfobj/src/elf_link.c#L2022-L2180), [`merge_relocations()`](file:///home/segin/substrate/usr.lib/elfobj/src/elf_link.c#L2182-L2231), or [`elf_link_resolve_symbol()`](file:///home/segin/substrate/usr.lib/elfobj/src/elf_link.c#L2517-L2547) is invoked, `libelfobj` shall first invoke [`elf__ensure_symbols_relocs()`](file:///home/segin/substrate/usr.lib/elfobj/src/elf_read.c#L1557-L1579) on the target object(s) before accessing `obj->symbols`, `obj->relocs`, or `sec->relocs`.
  - **Evidence:** [`elf_sections.c:430-561`](file:///home/segin/substrate/usr.lib/elfobj/src/elf_sections.c#L430-L561), [`elf_link.c:1929-2231`](file:///home/segin/substrate/usr.lib/elfobj/src/elf_link.c#L1929-L2231), [`elf_link.c:2517-2547`](file:///home/segin/substrate/usr.lib/elfobj/src/elf_link.c#L2517-L2547).
  - **Verification:** Open two `ET_REL` inputs with `ELFOBJ_OPEN_LAZY_PARSE`, pass them directly to `elf_link()` without calling `elf_symbol_count()` or `elf_reloc_count()` first, and verify all symbols and relocations are merged identically to eager parsing.

---

## 3. Bounds Checking, Integer Overflow & Parser Hardening

### 3.1 `.eh_frame_hdr` Bounds & Overflow Hardening (`ld_ehframe.c`)
- [ ] **[REQ-LD-EHF-001]** **WHEN** [`eh_record_count()`](file:///home/segin/substrate/usr.bin/ld/ld_ehframe.c#L19) or [`fill_eh_frame_hdr()`](file:///home/segin/substrate/usr.bin/ld/ld_ehframe.c#L159) validates a CIE/FDE record length `len`, it shall check `len == 0xffffffffu || len < 4 || (size_t)len > size - off - 4` (after `off + 4 <= size`) so that `off + 4 + (size_t)len` cannot wrap a 32-bit `size_t`.
  - **Evidence:** [`ld_ehframe.c:12-27`](file:///home/segin/substrate/usr.bin/ld/ld_ehframe.c#L12-L27), [`ld_ehframe.c:152-167`](file:///home/segin/substrate/usr.bin/ld/ld_ehframe.c#L152-L167).
  - **Verification:** Unit test `.eh_frame` containing a crafted `len = 0xfffffffc` on a 32-bit build (`-m32`).

- [ ] **[REQ-LD-EHF-002]** **WHEN** [`fill_eh_frame_hdr()`](file:///home/segin/substrate/usr.bin/ld/ld_ehframe.c#L170-L178) resolves an FDE's `cie_off`, it shall verify that `cie_off + 4 <= size` and that the CIE's `cie_len = read_u32_endian(data + cie_off, e)` satisfies `cie_len >= 4 && (size_t)cie_len <= size - cie_off - 4` before calling [`eh_cie_fde_encoding()`](file:///home/segin/substrate/usr.bin/ld/ld_ehframe.c#L64-L120).
  - **Evidence:** [`ld_ehframe.c:64-68`](file:///home/segin/substrate/usr.bin/ld/ld_ehframe.c#L64-L68), [`ld_ehframe.c:170-178`](file:///home/segin/substrate/usr.bin/ld/ld_ehframe.c#L170-L178).
  - **Verification:** Fuzz/regression test with an FDE pointing to a truncated or corrupted CIE length field.

- [ ] **[REQ-LD-EHF-003]** **WHEN** [`fill_eh_frame_hdr()`](file:///home/segin/substrate/usr.bin/ld/ld_ehframe.c#L180-L191) reads an FDE's initial location field at `data + off + 8`, it shall verify `len >= 4 + width` (where `width` is `4` for `DW_EH_PE_sdata4`/`udata4` or `8` for `DW_EH_PE_sdata8`/`udata8`) before calling `read_u32_endian()` or `read_u64_endian()`.
  - **Evidence:** [`ld_ehframe.c:180-191`](file:///home/segin/substrate/usr.bin/ld/ld_ehframe.c#L180-L191).
  - **Verification:** Regression test with a 4-byte FDE (`len == 4`, containing only CIE pointer and 0 bytes of PC begin) under AddressSanitizer.

### 3.2 `libelfobj` Note, Attribute, Merge & DWARF Parsers
- [ ] **[REQ-ELF-SEC-001]** **WHEN** [`read_x86_properties()`](file:///home/segin/substrate/usr.lib/elfobj/src/elf_merge.c#L497-L547) parses `.note.gnu.property` notes, it shall use overflow-safe arithmetic (`!elf__u64_add()` / subtraction against remaining size) when computing `desc` from `namesz` and advancing `p` by `psz`, preventing 32-bit `size_t` / `uint32_t` wraparound.
  - **Evidence:** [`elf_merge.c:511-539`](file:///home/segin/substrate/usr.lib/elfobj/src/elf_merge.c#L511-L539).
  - **Verification:** Unit test `elf_merge` with a malformed `.note.gnu.property` section having `namesz = 0xfffffffc` and `psz = 0xfffffffc` on a 32-bit build.

- [ ] **[REQ-ELF-SEC-002]** **WHEN** [`layout_group()`](file:///home/segin/substrate/usr.lib/elfobj/src/elf_merge.c#L354-L396) computes the merged blob size `cur` (`uint64_t`), it shall verify `cur <= SIZE_MAX` before calling `elf__calloc((size_t)cur, 1)`.
  - **Evidence:** [`elf_merge.c:367-378`](file:///home/segin/substrate/usr.lib/elfobj/src/elf_merge.c#L367-L378).
  - **Verification:** Code inspection and 32-bit overflow guard test.

- [ ] **[REQ-ELF-SEC-003]** **WHEN** [`parse_arm_attributes()`](file:///home/segin/substrate/usr.lib/elfobj/src/elf_gnu_ext.c#L371-L433) parses `.ARM.attributes` subsections and sub-subsections, it shall validate `sub_len >= 5 && sub_len <= end - sub_off` and `ss_len >= 5 && ss_len <= sub_end - pos` before computing `sub_end` and `ss_end`, preventing 32-bit `size_t` addition wraparound.
  - **Evidence:** [`elf_gnu_ext.c:399-415`](file:///home/segin/substrate/usr.lib/elfobj/src/elf_gnu_ext.c#L399-L415).
  - **Verification:** Regression test with crafted `.ARM.attributes` having `sub_len = 0xffffffff` on a 32-bit build.

- [ ] **[REQ-ELF-SEC-004]** **WHEN** [`validate_gnu_property_notes()`](file:///home/segin/substrate/usr.lib/elfobj/src/elf_validate.c#L202-L266) and [`validate_note_tables()`](file:///home/segin/substrate/usr.lib/elfobj/src/elf_validate.c#L329-L375) align offsets (`off = (off + 7u) & ~7u` and `off = (off + align - 1) & ~(align - 1)`), they shall guard against `size_t` wraparound when `off` is within `align - 1` bytes of `SIZE_MAX`.
  - **Evidence:** [`elf_validate.c:259-260`](file:///home/segin/substrate/usr.lib/elfobj/src/elf_validate.c#L259-L260), [`elf_validate.c:369-370`](file:///home/segin/substrate/usr.lib/elfobj/src/elf_validate.c#L369-L370).
  - **Verification:** Unit test `elf_validate()` with boundary-sized note descriptors.

- [ ] **[REQ-ELF-SEC-005]** **WHEN** [`elf_dwarf_parse_line()`](file:///home/segin/substrate/usr.lib/elfobj/src/elf_dwarf_line.c#L72-L210) parses a DWARF `.debug_line` header, **IF** `opcode_base == 0`, **THEN** [`elf_dwarf_parse_line()`](file:///home/segin/substrate/usr.lib/elfobj/src/elf_dwarf_line.c#L123) shall reject the malformed unit (returning `ELF_ERR_FORMAT` or breaking cleanly) rather than underflowing `opcode_base - 1` and treating `opcode == 0` (extended opcodes) as special opcodes (`opcode >= opcode_base`).
  - **Evidence:** [`elf_dwarf_line.c:122-124`](file:///home/segin/substrate/usr.lib/elfobj/src/elf_dwarf_line.c#L122-L124), [`elf_dwarf_line.c:163`](file:///home/segin/substrate/usr.lib/elfobj/src/elf_dwarf_line.c#L163).
  - **Verification:** Unit test `elf_dwarf_parse_line()` with a crafted DWARF line table where `opcode_base = 0`.

---

## 4. Linker Scripts, Version Scripts, TLS, Build-ID & State Hygiene

### 4.1 Linker Script & Version Script Parsing (`ld_script.c`, `ld_version.c`)
- [ ] **[REQ-LD-SCR-001]** **WHEN** [`lp_version()`](file:///home/segin/substrate/usr.bin/ld/ld_script.c#L1938-L2013) reconstructs the text of a `VERSION { ... }` block in a linker script, it shall save the previous token's `line` and `col` by value (`size_t prev_line, prev_col; int have_prev;`) before calling `lp_take(p)` and `lp_peek(p, 1)`, instead of keeping a `const lds_tok_t *prev` pointer to `p->tok` that is overwritten by the next `lp_peek(p, 1)` call.
  - **Evidence:** [`ld_script.c:1939-1998`](file:///home/segin/substrate/usr.bin/ld/ld_script.c#L1939-L1998).
  - **Verification:** Parse a linker script containing `VERSION { VER_1 { global: __vdso_*; local: *; }; }` and verify contiguous tokens are not split with spaces.

- [ ] **[REQ-LD-VER-001]** In [`ld_version_script_parse()`](file:///home/segin/substrate/usr.bin/ld/ld_version.c#L103-L161), the unused dead variable `in_extern` shall be removed (or used to track nested `extern "C++" { ... }` blocks consistently).
  - **Evidence:** [`ld_version.c:103, 130-133, 147-161`](file:///home/segin/substrate/usr.bin/ld/ld_version.c#L103-L161).
  - **Verification:** Clean compile and version script test with `extern "C++" { ... };`.

### 4.2 TLS Relocation Overflow Checks (`ld_reloc.c`)
- [ ] **[REQ-LD-TLS-001]** **WHEN** [`apply_tls_in_program()`](file:///home/segin/substrate/usr.bin/ld/ld_reloc.c#L1045-L1082) applies `R_X86_64_TPOFF32` or relaxes `R_X86_64_GOTTPOFF` on `EM_X86_64`, **IF** the signed 64-bit `tls_tpoff` value does not fit in a signed 32-bit integer (`< INT32_MIN` or `> INT32_MAX`), **THEN** [`apply_tls_in_program()`](file:///home/segin/substrate/usr.bin/ld/ld_reloc.c#L1045-L1082) shall report a relocation overflow error and return `-1` rather than silently truncating to 32 bits.
  - **Evidence:** [`ld_reloc.c:1045-1047`](file:///home/segin/substrate/usr.bin/ld/ld_reloc.c#L1045-L1047), [`ld_reloc.c:1080`](file:///home/segin/substrate/usr.bin/ld/ld_reloc.c#L1080).
  - **Verification:** Unit test `apply_tls_in_program()` with a `.tbss` section exceeding 2 GiB.

### 4.3 Build-ID Note Patching (`ld_buildid.c`)
- [ ] **[REQ-LD-BID-001]** **WHEN** [`write_build_id()`](file:///home/segin/substrate/usr.bin/ld/ld_buildid.c#L261-L343) locates the `.note.gnu.build-id` descriptor in the output file, it shall resolve the file offset from the output `elfobj_t`'s `.note.gnu.build-id` section (or scan at 1-byte granularity if section headers are unavailable) rather than scanning the entire file image in 4-byte steps (`at += 4`), which fails if a linker script places `.note.gnu.build-id` at a non-4-byte-aligned offset or if identical bytes appear earlier in `.rodata`/`.text`.
  - **Evidence:** [`ld_buildid.c:297-309`](file:///home/segin/substrate/usr.bin/ld/ld_buildid.c#L297-L309).
  - **Verification:** Link a binary with `--build-id=sha1` using a linker script that places `.note.gnu.build-id` at an odd file offset, and verify the build-id note is populated.

### 4.4 Static State & Memory Hygiene (`ld_dynamic.c`, `ld_input.c`, `ld_reloc.c`, `elf_gnu_ext.c`, `elf_reloc.c`)
- [ ] **[REQ-LD-MEM-001]** **WHEN** [`plan_dynamic_needed()`](file:///home/segin/substrate/usr.bin/ld/ld_dynamic.c#L1899-L2028) completes (or in [`ctx_free()`](file:///home/segin/substrate/usr.bin/ld/ld_util.c#L525-L561)), `ld` shall call [`dynstr_index_reset()`](file:///home/segin/substrate/usr.bin/ld/ld_dynamic.c#L60-L70) so that the `dynstr_index` hash table and its duplicated strings are not leaked at process exit.
  - **Evidence:** [`ld_dynamic.c:48-70, 1899`](file:///home/segin/substrate/usr.bin/ld/ld_dynamic.c#L48-L70).
  - **Verification:** Run a dynamic link under LeakSanitizer / Valgrind `--show-leak-kinds=all` and verify 0 still-reachable blocks from `dynstr_index`.

- [ ] **[REQ-LD-MEM-002]** **WHEN** [`load_library_input()`](file:///home/segin/substrate/usr.bin/ld/ld_input.c#L823-L942) begins a library search, it shall clear `lib_passed_over[0] = '\0'` at entry, and [`link_once()`](file:///home/segin/substrate/usr.bin/ld/ld.c#L350-L539) shall reset `ifunc_ctx = NULL` on exit so stale pointers do not persist across relayout passes.
  - **Evidence:** [`ld_input.c:815-828`](file:///home/segin/substrate/usr.bin/ld/ld_input.c#L815-L828), [`ld_reloc.c:100, 449`](file:///home/segin/substrate/usr.bin/ld/ld_reloc.c#L100).
  - **Verification:** Code inspection and multi-pass relayout test.

- [ ] **[REQ-ELF-THR-001]** [`elf_reloc_name_for_machine()`](file:///home/segin/substrate/usr.lib/elfobj/src/elf_reloc.c#L3034-L3217) shall return `"R_AARCH64_NONE"` when `machine == EM_AARCH64 && type == R_AARCH64_NONE` (`0`), and [`elf_arm_attribute_string_at()`](file:///home/segin/substrate/usr.lib/elfobj/src/elf_gnu_ext.c#L439-L473) shall store attribute strings inside the owning `elfobj_t` (freed in [`elf_close()`](file:///home/segin/substrate/usr.lib/elfobj/src/elf_util.c#L872-L926)) instead of a non-thread-safe process-wide `static char *g_last_attr_string`.
  - **Evidence:** [`elf_reloc.c:3092-3095`](file:///home/segin/substrate/usr.lib/elfobj/src/elf_reloc.c#L3092-L3095), [`elf_gnu_ext.c:9, 468-470`](file:///home/segin/substrate/usr.lib/elfobj/src/elf_gnu_ext.c#L9).
  - **Verification:** Unit test `elf_reloc_name_for_machine(EM_AARCH64, R_AARCH64_NONE)` and multi-attribute reads via `elf_arm_attribute_string_at()`.

---

## 5. Documentation & Specification Synchronization

- [ ] **[REQ-LD-DOC-001]** [`usr.bin/ld/SPEC.md`](file:///home/segin/substrate/usr.bin/ld/SPEC.md#L30-L42) §0 ("Where this file and the linker differ") shall be updated to remove features that are now implemented (`--build-id` in [`ld_buildid.c`](file:///home/segin/substrate/usr.bin/ld/ld_buildid.c), `--version-script` in [`ld_version.c`](file:///home/segin/substrate/usr.bin/ld/ld_version.c), archive symbol-index extraction in [`ld_input.c:392-476`](file:///home/segin/substrate/usr.bin/ld/ld_input.c#L392-L476), `SORT_*` in [`ld_script.c:3237-3264`](file:///home/segin/substrate/usr.bin/ld/ld_script.c#L3237-L3264), and multi-bucket `.gnu.hash` in [`ld_dynamic.c:351-459`](file:///home/segin/substrate/usr.bin/ld/ld_dynamic.c#L351-L459)).
  - **Evidence:** [`usr.bin/ld/SPEC.md:30-42`](file:///home/segin/substrate/usr.bin/ld/SPEC.md#L30-L42).

- [ ] **[REQ-LD-DOC-002]** [`usr.bin/ld/ARCHITECTURE.md`](file:///home/segin/substrate/usr.bin/ld/ARCHITECTURE.md#L8-L35) shall be updated to list all 18 translation units (adding [`ld_version.c`](file:///home/segin/substrate/usr.bin/ld/ld_version.c) and [`ld_buildid.c`](file:///home/segin/substrate/usr.bin/ld/ld_buildid.c)) and to update the performance notes at [line 195](file:///home/segin/substrate/usr.bin/ld/ARCHITECTURE.md#L195) to reflect archive symbol-index lookup and multi-bucket `.gnu.hash` generation.
  - **Evidence:** [`usr.bin/ld/ARCHITECTURE.md:8-35, 167, 195`](file:///home/segin/substrate/usr.bin/ld/ARCHITECTURE.md#L8-L35).

- [ ] **[REQ-LD-DOC-003]** [`usr.man/man1/ld.1`](file:///home/segin/substrate/usr.man/man1/ld.1#L952-L975) lines 952–975 shall be updated to remove `VERSION`, `AT`, and `BYTE`/`SHORT`/`LONG`/`QUAD` from the "Read and not acted on" / "Refused" lists, matching their implemented descriptions at lines 671, 763, and 814–827.
  - **Evidence:** [`usr.man/man1/ld.1:952-975`](file:///home/segin/substrate/usr.man/man1/ld.1#L952-L975).

---

## 6. Archive Maintainer & Ranlib Audit (`usr.bin/ar`)

### 6.1 Critical Archive Symbol Index (`/`, `/SYM64/`, `__.SYMDEF SORTED`) Bugs
- [ ] **[REQ-AR-SYM-001]** **WHEN** [`write_archive()`](file:///home/segin/substrate/usr.bin/ar/ar.c#L1642-L1918) computes member file offsets (`current_off`) for a GNU-format archive containing a symbol table (`symdef`), it shall size `symdef` accurately (`4 + 4 * count + strsize` for 32-bit `/` when all member offsets fit in `0xffffffffULL`, or `8 + 8 * count + strsize` for `/SYM64/` when any member offset exceeds `0xffffffffULL`) **before** assigning final member offsets, rather than reserving `/SYM64/` (`8 + 8 * count + strsize`) in [`ranlib()`](file:///home/segin/substrate/usr.bin/ar/ar.c#L1539-L1541), computing `current_off` using that larger size at [`ar.c:1664`](file:///home/segin/substrate/usr.bin/ar/ar.c#L1664), and then shrinking `symdef->size` to `4 + 4 * count + strsize` at [`ar.c:1750-1759`](file:///home/segin/substrate/usr.bin/ar/ar.c#L1750-L1759) (which shifts every recorded member offset in `/` forward by `4 * (count + 1)` bytes and causes [`usr.bin/ld`](file:///home/segin/substrate/usr.bin/ld/ld_input.c#L466) to miss all archive members).
  - **Evidence:** [`usr.bin/ar/ar.c:1539-1541`](file:///home/segin/substrate/usr.bin/ar/ar.c#L1539-L1541), [`usr.bin/ar/ar.c:1664`](file:///home/segin/substrate/usr.bin/ar/ar.c#L1664), [`usr.bin/ar/ar.c:1723-1759`](file:///home/segin/substrate/usr.bin/ar/ar.c#L1723-L1759), [`usr.bin/ld/ld_input.c:414, 466`](file:///home/segin/substrate/usr.bin/ld/ld_input.c#L466).
  - **Verification:** Build `libfoo.a` with `usr.bin/ar rcs libfoo.a foo.o`, inspect the `/` symbol table offsets, verify each offset equals the exact file offset (`8 + 60 + symdef_aligned_size + ...`) of its member header, and link an executable against `libfoo.a` using `usr.bin/ld`.

- [ ] **[REQ-AR-SYM-002]** **WHEN** [`write_archive()`](file:///home/segin/substrate/usr.bin/ar/ar.c#L1648-L1653) checks whether `head` is an archive symbol table member, it shall recognize `RANLIBSORT` (`"__.SYMDEF SORTED"`, created by [`ranlib()`](file:///home/segin/substrate/usr.bin/ar/ar.c#L1552) when `archive_format == ARFMT_BSD`) in addition to `RANLIBMAG` (`"__.SYMDEF"`), `"/"`, and `"/SYM64/"`, and when serializing `"__.SYMDEF SORTED"` in BSD mode it shall emit the member name correctly so `symdef` is populated rather than written as an all-zero payload.
  - **Evidence:** [`usr.bin/ar/ar.c:1552`](file:///home/segin/substrate/usr.bin/ar/ar.c#L1552), [`usr.bin/ar/ar.c:1648-1653`](file:///home/segin/substrate/usr.bin/ar/ar.c#L1648-L1653), [`usr.bin/ar/ar.c:1770-1792`](file:///home/segin/substrate/usr.bin/ar/ar.c#L1770-L1792), [`usr.bin/ar/ar.c:1869-1875`](file:///home/segin/substrate/usr.bin/ar/ar.c#L1869-L1875).
  - **Verification:** Create a BSD archive (`AR_BSD=1 ar rcs libbsd.a foo.o`), verify `symdef` is populated with `ranlib_bsd` entries and string table, and verify `read_archive()` recognizes it as a symbol table on re-read.

- [ ] **[REQ-AR-SYM-003]** **WHEN** a mutating archive operation (`-r`, `-d`, `-m`, `-q`, or MRI `ADDMOD`/`DELETE`/`REPLACE`) modifies an archive that already contains a symbol table (`/` or `__.SYMDEF*`) and `-S` (`MOD_NOSYMTAB`) is not specified, `ar` shall regenerate the symbol table via [`ranlib()`](file:///home/segin/substrate/usr.bin/ar/ar.c#L1506-L1579) (or reallocate `symdef->data` and recompute `symdef->size` before member offset assignment in [`write_archive()`](file:///home/segin/substrate/usr.bin/ar/ar.c#L1658-L1792)), preventing heap buffer overflow in BSD mode ([`ar.c:1777`](file:///home/segin/substrate/usr.bin/ar/ar.c#L1777)) and stale `symdef->size` offset miscalculation in GNU mode ([`ar.c:1664`](file:///home/segin/substrate/usr.bin/ar/ar.c#L1664)).
  - **Evidence:** [`usr.bin/ar/ar.c:323-356`](file:///home/segin/substrate/usr.bin/ar/ar.c#L323-L356), [`usr.bin/ar/ar.c:1648-1792`](file:///home/segin/substrate/usr.bin/ar/ar.c#L1648-L1792).
  - **Verification:** Create `lib.a` with `ar rcs lib.a a.o`, then run `ar r lib.a b.o` (without `-s`) where `b.o` defines new symbols; verify no heap overflow under AddressSanitizer and verify all offsets in `/` are valid.

- [ ] **[REQ-AR-SYM-004]** **WHERE** thin archive mode (`-T` / `archive_is_thin`) is active, **WHEN** [`ranlib()`](file:///home/segin/substrate/usr.bin/ar/ar.c#L1506-L1579) and [`collect_archive_symbols()`](file:///home/segin/substrate/usr.bin/ar/ar.c#L1354-L1504) build the archive symbol index, `ar` shall open each external thin member file (`cur->thin_path`, resolved relative to the archive directory) to extract ELF symbols and emit a GNU `/` symbol table rather than returning early at [`ar.c:1508`](file:///home/segin/substrate/usr.bin/ar/ar.c#L1508).
  - **Evidence:** [`usr.bin/ar/ar.c:1369`](file:///home/segin/substrate/usr.bin/ar/ar.c#L1369), [`usr.bin/ar/ar.c:1508`](file:///home/segin/substrate/usr.bin/ar/ar.c#L1508).
  - **Verification:** Run `ar rcsT thin.a a.o b.o` and verify `thin.a` contains a valid `/` symbol map consumable by `nm -s` and `ld`.

### 6.2 Thin Archive Path Resolution & Corrupt Archive Diagnostics
- [ ] **[REQ-AR-THIN-001]** **WHERE** `archive_is_thin` is true, **WHEN** [`print_members()`](file:///home/segin/substrate/usr.bin/ar/ar.c#L1152-L1166), [`extract_members()`](file:///home/segin/substrate/usr.bin/ar/ar.c#L1229-L1244), or [`list_members()`](file:///home/segin/substrate/usr.bin/ar/ar.c#L1343-L1346) accesses a relative `cur->thin_path`, `ar` shall resolve `cur->thin_path` relative to `dirname(archive_name)` (when `cur->thin_path[0] != '/'`) and strip any trailing newline from thin payload paths in [`read_archive()`](file:///home/segin/substrate/usr.bin/ar/ar.c#L901-L905).
  - **Evidence:** [`usr.bin/ar/ar.c:901-905`](file:///home/segin/substrate/usr.bin/ar/ar.c#L901-L905), [`usr.bin/ar/ar.c:1152-1166`](file:///home/segin/substrate/usr.bin/ar/ar.c#L1152-L1166), [`usr.bin/ar/ar.c:1229-1244`](file:///home/segin/substrate/usr.bin/ar/ar.c#L1229-L1244), [`usr.bin/ar/ar.c:1343-1346`](file:///home/segin/substrate/usr.bin/ar/ar.c#L1343-L1346).
  - **Verification:** Create a thin archive in a subdirectory (`subdir/lib.a`) referencing `sub.o`, invoke `ar p subdir/lib.a` and `ar x subdir/lib.a` from the parent directory, and verify member contents are read without `No such file or directory`.

- [ ] **[REQ-AR-PARSE-001]** **IF** [`read_archive()`](file:///home/segin/substrate/usr.bin/ar/ar.c#L714-L934) encounters a member header with an invalid `ARFMAG` trailer ([`ar.c:730-733`](file:///home/segin/substrate/usr.bin/ar/ar.c#L730-L733)), unparseable numeric fields ([`ar.c:752-756`](file:///home/segin/substrate/usr.bin/ar/ar.c#L752-L756)), or a truncated header/payload ([`ar.c:722, 811, 886`](file:///home/segin/substrate/usr.bin/ar/ar.c#L722)), **THEN** `ar` shall abort archive parsing and exit with a non-zero status rather than continuing to read misaligned 60-byte chunks or returning `0`.
  - **Evidence:** [`usr.bin/ar/ar.c:722-756`](file:///home/segin/substrate/usr.bin/ar/ar.c#L722-L756), [`usr.bin/ar/ar.c:811, 886`](file:///home/segin/substrate/usr.bin/ar/ar.c#L811).
  - **Verification:** Run `ar t` on an archive with corrupted `ar_fmag` or truncated member payload and verify `ar` exits with status `1`.

- [ ] **[REQ-AR-PARSE-002]** **IF** [`parse_ar_name_field()`](file:///home/segin/substrate/usr.bin/ar/ar.c#L602-L645) encounters a GNU extended name reference `/<offset>` (`tmp[0] == '/' && isdigit(tmp[1])`) where `gnu_name_table == NULL` or `off >= gnu_name_table_size`, **THEN** [`parse_ar_name_field()`](file:///home/segin/substrate/usr.bin/ar/ar.c#L602-L645) shall reject the malformed member header (returning `NULL`) rather than falling through to `slash = strchr(tmp, '/'); *slash = '\0';` and silently naming the member `""`.
  - **Evidence:** [`usr.bin/ar/ar.c:605-631`](file:///home/segin/substrate/usr.bin/ar/ar.c#L605-L631).
  - **Verification:** Test `ar t` on a crafted archive with `ar_name = "/999999         "` and verify `ar` reports a malformed archive error instead of listing an empty member name.

---

## 7. Symbol Table Inspector Audit (`usr.bin/nm`)

### 7.1 Symbol Classification, Sorting & Filtering Bugs (`nm.c`)
- [ ] **[REQ-NM-SORT-001]** **WHERE** `--size-sort` is enabled, [`cmp_size()`](file:///home/segin/substrate/usr.bin/nm/nm.c#L369-L386) and [`collect_symbols()`](file:///home/segin/substrate/usr.bin/nm/nm.c#L584-L664) shall exclude undefined symbols (`SHN_UNDEF`), sort symbols in ascending order of size with deterministic name tie-breaking (or match [`usr.bin/nm/nm.1:118`](file:///home/segin/substrate/usr.bin/nm/nm.1#L118) after reconciling documentation), and display symbol sizes in BSD output mode even when `-S` is not separately specified.
  - **Evidence:** [`usr.bin/nm/nm.c:369-386`](file:///home/segin/substrate/usr.bin/nm/nm.c#L369-L386), [`usr.bin/nm/nm.c:584-664`](file:///home/segin/substrate/usr.bin/nm/nm.c#L584-L664), [`usr.bin/nm/nm.1:118`](file:///home/segin/substrate/usr.bin/nm/nm.1#L118).
  - **Verification:** Run `nm --size-sort` on an object containing undefined symbols and multiple sized symbols; verify `U` symbols are omitted and sizes are displayed consistently with documentation.

- [ ] **[REQ-NM-CLASS-001]** **WHEN** [`classify_symbol()`](file:///home/segin/substrate/usr.bin/nm/nm.c#L171-L260) classifies a symbol in a debug section (`elf_section_is_debug(sec) != 0`), it shall return `'n'` for `STB_LOCAL` and `'N'` for non-local bindings, and **WHEN** classifying `STT_GNU_IFUNC` (`10`) or `STB_GNU_UNIQUE` (`10`) symbols, [`classify_symbol()`](file:///home/segin/substrate/usr.bin/nm/nm.c#L171-L260) and [`stt_name()`](file:///home/segin/substrate/usr.bin/nm/nm.c#L527-L538) shall classify them as `'i'`/`'I'` (`"IFUNC"`) and `'u'` (and `"COMMON"` for `STT_COMMON`).
  - **Evidence:** [`usr.bin/nm/nm.c:171-260`](file:///home/segin/substrate/usr.bin/nm/nm.c#L171-L260), [`usr.bin/nm/nm.c:527-538`](file:///home/segin/substrate/usr.bin/nm/nm.c#L527-L538).
  - **Verification:** Run `nm` and `nm -f sysv` on an object containing local debug symbols, `STT_GNU_IFUNC`, and `STT_COMMON` symbols.

- [ ] **[REQ-NM-OPT-001]** **WHERE** `-s` / `--print-armap` is enabled (`opts->print_armap == 1`), **WHEN** [`process_archive()`](file:///home/segin/substrate/usr.bin/nm/nm.c#L1044-L1190) processes an archive containing aGNU (`/`, `/SYM64/`) or BSD (`__.SYMDEF*`) symbol map, `nm` shall parse and print the `Archive index:` mapping (`<symbol> in <member_name>`) before listing member symbols, rather than ignoring `opts->print_armap`.
  - **Evidence:** [`usr.bin/nm/nm.c:85`](file:///home/segin/substrate/usr.bin/nm/nm.c#L85), [`usr.bin/nm/nm.c:1044-1190`](file:///home/segin/substrate/usr.bin/nm/nm.c#L1044-L1190), [`usr.bin/nm/nm.c:1321, 1362`](file:///home/segin/substrate/usr.bin/nm/nm.c#L1321).
  - **Verification:** Run `nm -s libfoo.a` and verify `Archive index:` is printed with symbol-to-member mappings.

- [ ] **[REQ-NM-OPT-002]** **WHERE** `--special-syms` is not enabled (`opts->special_syms == 0`), **WHEN** [`collect_symbols()`](file:///home/segin/substrate/usr.bin/nm/nm.c#L597-L639) filters symbols on ARM/AArch64/RISC-V objects, `nm` shall suppress architecture mapping symbols (`$a`, `$t`, `$d`, `$x`, and `$a.*`/`$t.*`/`$d.*`/`$x.*`) unless `opts->special_syms != 0` or `-a` is active.
  - **Evidence:** [`usr.bin/nm/nm.c:84`](file:///home/segin/substrate/usr.bin/nm/nm.c#L84), [`usr.bin/nm/nm.c:597-639`](file:///home/segin/substrate/usr.bin/nm/nm.c#L597-L639), [`usr.bin/nm/nm.c:1322`](file:///home/segin/substrate/usr.bin/nm/nm.c#L1322).
  - **Verification:** Run `nm` vs `nm --special-syms` on an ARM/AArch64 object containing `$a`/`$d`/`$x` mapping symbols.

### 7.2 CLI Option Parsing & Archive Bounds Hardening (`nm.c`)
- [ ] **[REQ-NM-CLI-001]** **WHEN** [`main()`](file:///home/segin/substrate/usr.bin/nm/nm.c#L1291-L1424) parses command-line arguments, it shall:
  1. Recognize `--` as the end-of-options delimiter in both the option-parsing loop and the file-execution loop;
  2. Recognize `--line-numbers` as the long option alias for `-l`;
  3. Correctly skip the separate argument of a bundled `-f` or `-t` short option (e.g., `nm -at d foo.o` or `nm -af sysv foo.o`) during the file-counting and file-processing passes instead of only checking `strcmp(argv[i], "-t") == 0` at [`nm.c:1416`](file:///home/segin/substrate/usr.bin/nm/nm.c#L1416).
  - **Evidence:** [`usr.bin/nm/nm.c:1291-1422`](file:///home/segin/substrate/usr.bin/nm/nm.c#L1291-L1422).
  - **Verification:** Run `nm -at d foo.o`, `nm --line-numbers foo.o`, and `nm -- -foo.o`; verify none misinterpret option arguments or `--` as filenames or unknown flags.

- [ ] **[REQ-NM-AR-001]** **WHEN** [`process_archive()`](file:///home/segin/substrate/usr.bin/nm/nm.c#L1107-L1135) advances `next_off` across an archive member, it shall compute `next_off` using `payload_size` (which is `0` for external thin archive members and `msize` for embedded members) and shall guard `(payload_size & 1u)` against `size_t` addition overflow, rather than computing `next_off = payload_off + msize + (msize & 1u)` at [`nm.c:1129`](file:///home/segin/substrate/usr.bin/nm/nm.c#L1129) where an unchecked `msize` in a thin archive skips subsequent headers or wraps `size_t` into an infinite loop.
  - **Evidence:** [`usr.bin/nm/nm.c:1110-1135`](file:///home/segin/substrate/usr.bin/nm/nm.c#L1110-L1135).
  - **Verification:** Run `nm` on a multi-member thin archive (`!<thin>\n`) and on a crafted thin archive with `msize = 0xffffffff` on a 32-bit build; verify all thin members are visited and no infinite loop occurs.

- [ ] **[REQ-NM-MEM-001]** **WHEN** [`main()`](file:///home/segin/substrate/usr.bin/nm/nm.c#L1423) exits, `nm` shall free the last demangled string cached in `prev_demangled` inside [`display_name()`](file:///home/segin/substrate/usr.bin/nm/nm.c#L423-L440) (or pass a caller-owned buffer/cleanup hook).
  - **Evidence:** [`usr.bin/nm/nm.c:423-440`](file:///home/segin/substrate/usr.bin/nm/nm.c#L423-L440).
  - **Verification:** Run `nm -C` on a C++ binary under LeakSanitizer / Valgrind `--show-leak-kinds=all` and verify 0 still-reachable demangled buffers.

---

## 8. DWARF Address-to-Line Resolver Audit (`usr.bin/addr2line`)

### 8.1 Critical DWARF v5 & Multi-CU Resolution Bugs (`addr2line.c`)
- [ ] **[REQ-A2L-DW5-001]** **WHEN** [`line_path_for_unit_file()`](file:///home/segin/substrate/usr.bin/addr2line/addr2line.c#L3633-L3676) resolves a `file_index` and `f->dir_index` for a `.debug_line` unit `u`, it shall use **0-based** indexing into `u->files[file_index]` and `u->include_dirs[f->dir_index]` when `u->version >= 5` (per DWARF5 §6.2.4 / §6.2.5.1) and **1-based** indexing (`u->files[file_index - 1]`, `u->include_dirs[f->dir_index - 1]`) when `u->version < 5`, rather than unconditionally treating `file_index == 0` as invalid and subtracting `1` from `file_index` and `f->dir_index`.
  - **Evidence:** [`usr.bin/addr2line/addr2line.c:959-1012`](file:///home/segin/substrate/usr.bin/addr2line/addr2line.c#L959-L1012), [`usr.bin/addr2line/addr2line.c:3646-3663`](file:///home/segin/substrate/usr.bin/addr2line/addr2line.c#L3646-L3663).
  - **Verification:** Compile a multi-file C program with `gcc -gdwarf-5`, resolve addresses in both the primary source file (`file_index == 0`) and an included header (`file_index == 1`), and verify `addr2line` outputs the exact directory and filename instead of `??:0` or an off-by-one file.

- [ ] **[REQ-A2L-DW5-002]** **WHEN** [`parse_line_form_value()`](file:///home/segin/substrate/usr.bin/addr2line/addr2line.c#L805-L926) resolves a `DW_FORM_strp` or `DW_FORM_line_strp` attribute, **IF** [`lookup_debug_str()`](file:///home/segin/substrate/usr.bin/addr2line/addr2line.c#L792-L803) returns `NULL` (missing string section or out-of-bounds offset), **THEN** `parse_line_form_value()` shall return `-1` rather than returning `0` with `*out_string == NULL` and silently replacing `DW_LNCT_path` with `""` in [`parse_line_v5_table()`](file:///home/segin/substrate/usr.bin/addr2line/addr2line.c#L980-L996).
  - **Evidence:** [`usr.bin/addr2line/addr2line.c:864-892`](file:///home/segin/substrate/usr.bin/addr2line/addr2line.c#L864-L892), [`usr.bin/addr2line/addr2line.c:980-996`](file:///home/segin/substrate/usr.bin/addr2line/addr2line.c#L980-L996).
  - **Verification:** Unit test `parse_line_v5_table()` with an out-of-bounds `DW_FORM_line_strp` offset and verify it rejects the malformed table entry.

- [ ] **[REQ-A2L-REF-001]** **WHEN** [`parse_debug_info_subprograms()`](file:///home/segin/substrate/usr.bin/addr2line/addr2line.c#L2742-L3270) parses `DW_AT_abstract_origin` or `DW_AT_specification` attributes, **IF** the attribute form is CU-relative (`DW_FORM_ref1`, `DW_FORM_ref2`, `DW_FORM_ref4`, `DW_FORM_ref8`, or `DW_FORM_ref_udata`), **THEN** `addr2line` shall add the current compilation unit's header start offset (`(uint64_t)(unit_start - img->debug_info.data)`) to `ref` before storing `abstract_origin` / `specification`, so that references match the section-relative `die_offset` stored in `img->die_names` across all compilation units (not only CU #0 at offset 0).
  - **Evidence:** [`usr.bin/addr2line/addr2line.c:836-906`](file:///home/segin/substrate/usr.bin/addr2line/addr2line.c#L836-L906), [`usr.bin/addr2line/addr2line.c:2742, 2804`](file:///home/segin/substrate/usr.bin/addr2line/addr2line.c#L2804), [`usr.bin/addr2line/addr2line.c:3085-3123`](file:///home/segin/substrate/usr.bin/addr2line/addr2line.c#L3085-L3123).
  - **Verification:** Link a binary from two C/C++ object files (`a.o` and `b.o`) where `b.o` (CU #1, non-zero `.debug_info` offset) contains inlined functions (`DW_AT_abstract_origin`) and out-of-line C++ methods (`DW_AT_specification`); verify `addr2line -f -i` resolves function names in `b.o` rather than printing `??`.

- [ ] **[REQ-A2L-SUB-001]** **WHEN** [`parse_debug_info_subprograms()`](file:///home/segin/substrate/usr.bin/addr2line/addr2line.c#L3157-L3200) records a `DW_TAG_subprogram` DIE that has address ranges (`has_ranges` or `has_low_pc && has_high_pc`) but inherits its name via `DW_AT_abstract_origin` or `DW_AT_specification`, `dwarf_subprogram_t` and [`lookup_function_name()`](file:///home/segin/substrate/usr.bin/addr2line/addr2line.c#L3275-L3300) shall resolve the subprogram's name via [`resolve_die_name_ref()`](file:///home/segin/substrate/usr.bin/addr2line/addr2line.c#L3324-L3348).
  - **Evidence:** [`usr.bin/addr2line/addr2line.c:3157-3200`](file:///home/segin/substrate/usr.bin/addr2line/addr2line.c#L3157-L3200), [`usr.bin/addr2line/addr2line.c:3288-3295`](file:///home/segin/substrate/usr.bin/addr2line/addr2line.c#L3288-L3295).
  - **Verification:** Run `addr2line -f -C` on a stripped-symtab C++ binary with `.debug_info` containing out-of-line class method definitions (`DW_TAG_subprogram` with `DW_AT_specification`) and verify the demangled method name is printed instead of `??`.

- [ ] **[REQ-A2L-INL-001]** **WHEN** [`output_lookup()`](file:///home/segin/substrate/usr.bin/addr2line/addr2line.c#L3685-L3706) and [`output_inline_chain()`](file:///home/segin/substrate/usr.bin/addr2line/addr2line.c#L3405-L3429) resolve an address `query` that falls inside an inlined subroutine chain `chain[0 .. n-1]` (innermost to outermost) inside enclosing subprogram `func`:
  1. **Without `-i` (`opts->show_inlines == 0`):** `addr2line` shall emit the **innermost** function name (`inline_function_name(img, &img->inlines[chain[0]])` when `n > 0`, else `func`) paired with the `.debug_line` location (`entry->file`, `entry->line`, `entry->column`);
  2. **With `-i` (`opts->show_inlines != 0`):** `addr2line` shall emit:
     - Frame `0` (innermost): `inline_function_name(chain[0])` at `(entry->file, entry->line, entry->column)`;
     - Frame `k` (`1 <= k < n`): `inline_function_name(chain[k])` at `(chain[k-1].call_file, chain[k-1].call_line, chain[k-1].call_column)`;
     - Frame `n` (enclosing subprogram): `func` at `(chain[n-1].call_file, chain[n-1].call_line, chain[n-1].call_column)`, and omit the `0x<addr>` prefix on inline unwind frames `1 .. n`.
  - **Evidence:** [`usr.bin/addr2line/addr2line.c:3405-3429`](file:///home/segin/substrate/usr.bin/addr2line/addr2line.c#L3405-L3429), [`usr.bin/addr2line/addr2line.c:3685-3706`](file:///home/segin/substrate/usr.bin/addr2line/addr2line.c#L3685-L3706).
  - **Verification:** Compare `usr.bin/addr2line -f -i` and `usr.bin/addr2line -f -i -p -a` output against GNU `addr2line` on a two-level inlined function call; verify every function name is paired with its own source line rather than shifted by one frame.

### 8.2 Line Range Lookup, Memory Leaks & CLI Parsing (`addr2line.c`)
- [ ] **[REQ-A2L-LINE-001]** **WHEN** [`build_line_entries()`](file:///home/segin/substrate/usr.bin/addr2line/addr2line.c#L1423-L1457) constructs `[low, high)` intervals from `.debug_line` rows, **IF** a line unit ends without a `DW_LNE_end_sequence` row, it shall cap the final row's `entry.high` to `entry.low + 1u` (instead of `UINT64_MAX`), and [`lookup_line_entry()`](file:///home/segin/substrate/usr.bin/addr2line/addr2line.c#L1459-L1495) shall handle overlapping or zero-relocated sequences without aborting the backward scan prematurely at [`addr2line.c:1489`](file:///home/segin/substrate/usr.bin/addr2line/addr2line.c#L1489).
  - **Evidence:** [`usr.bin/addr2line/addr2line.c:1438-1440`](file:///home/segin/substrate/usr.bin/addr2line/addr2line.c#L1438-L1440), [`usr.bin/addr2line/addr2line.c:1481-1493`](file:///home/segin/substrate/usr.bin/addr2line/addr2line.c#L1481-L1493).
  - **Verification:** Unit test `build_line_entries()` on a truncated line program missing `DW_LNE_end_sequence` and verify high addresses beyond the unit do not falsely match the last row.

- [ ] **[REQ-A2L-MEM-001]** **IF** `skip_form_value()` fails inside the attribute loop of [`parse_debug_info_subprograms()`](file:///home/segin/substrate/usr.bin/addr2line/addr2line.c#L2924-L3110), **THEN** `parse_debug_info_subprograms()` shall call `free(name)` in addition to `free(linkage_name)` before returning `-1`, and the redundant post-transfer `image_add_die_name()` calls at [`addr2line.c:3189-3198`](file:///home/segin/substrate/usr.bin/addr2line/addr2line.c#L3189-L3198) and [`addr2line.c:3242-3250`](file:///home/segin/substrate/usr.bin/addr2line/addr2line.c#L3242-L3250) (after `sp`/`inl` have been zeroed by `image_append_*`) shall be removed.
  - **Evidence:** [`usr.bin/addr2line/addr2line.c:2931, 2965, 2999, 3032, 3065, 3107, 3189-3198, 3242-3250`](file:///home/segin/substrate/usr.bin/addr2line/addr2line.c#L2931).
  - **Verification:** Run `addr2line` under AddressSanitizer/LeakSanitizer on malformed `.debug_info` inputs where `DW_AT_name` precedes a truncated attribute.

- [ ] **[REQ-A2L-ELF-001]** [`parse_elf_metadata()`](file:///home/segin/substrate/usr.bin/addr2line/addr2line.c#L2080-L2249) shall query section addresses and `PT_LOAD` virtual addresses from the already-opened `img->elf` via `libelfobj` APIs (or validate `ftell()` errors, allow 52-byte `ELFCLASS32` files at [`addr2line.c:2123`](file:///home/segin/substrate/usr.bin/addr2line/addr2line.c#L2123), and use overflow-safe bounds checks for `phoff`, `shoff`, and `strtab_off + strtab_size` at [`addr2line.c:2169-2215`](file:///home/segin/substrate/usr.bin/addr2line/addr2line.c#L2169-L2215)) instead of re-reading the file from disk with unchecked 64-bit offset additions.
  - **Evidence:** [`usr.bin/addr2line/addr2line.c:2080-2249`](file:///home/segin/substrate/usr.bin/addr2line/addr2line.c#L2080-L2249).
  - **Verification:** Test `addr2line` on 32-bit and 64-bit ELF files and crafted headers with `shoff = UINT64_MAX - 8`.

- [ ] **[REQ-A2L-CLI-001]** **WHEN** [`parse_options()`](file:///home/segin/substrate/usr.bin/addr2line/addr2line.c#L3743-L3861) parses command-line flags, it shall support bundled short options (e.g., `-fpe a.out`, `-fpCia`), attached option arguments (`-e<path>`, `-j<sec>`), `--exe <path>`, and `--demangle=<style>`, and **WHEN** an invalid address argument is encountered in [`resolve_stdin()`](file:///home/segin/substrate/usr.bin/addr2line/addr2line.c#L3713-L3717) or [`resolve_argv()`](file:///home/segin/substrate/usr.bin/addr2line/addr2line.c#L3732-L3736), it shall format the unresolved output according to active `-f`/`-a`/`-p`/`-c` flags via [`emit_frame_result()`](file:///home/segin/substrate/usr.bin/addr2line/addr2line.c#L3546-L3595).
  - **Evidence:** [`usr.bin/addr2line/addr2line.c:3530-3532`](file:///home/segin/substrate/usr.bin/addr2line/addr2line.c#L3530-L3532), [`usr.bin/addr2line/addr2line.c:3713-3736`](file:///home/segin/substrate/usr.bin/addr2line/addr2line.c#L3713-L3736), [`usr.bin/addr2line/addr2line.c:3743-3861`](file:///home/segin/substrate/usr.bin/addr2line/addr2line.c#L3743-L3861).
  - **Verification:** Run `addr2line -fpC -e a.out 0x1000` and `addr2line -fp invalid_hex` and verify option parsing and formatted fallback output.

---

## 9. C++/Rust/D Symbol Demangler Filter Audit (`usr.bin/cxxfilt`)

### 9.1 Underscore Stripping (`-n` / `-_`), Token Boundaries & `-t` Stream Mode (`cxxfilt.c`)
- [ ] **[REQ-CXXF-STRIP-001]** **WHERE** `-n` / `--no-strip-underscore` (`cfg->strip == STRIP_NONE`) is active, [`demangle_with_cfg()`](file:///home/segin/substrate/usr.bin/cxxfilt/cxxfilt.c#L406-L459) shall **not** strip a leading underscore at [`cxxfilt.c:451-456`](file:///home/segin/substrate/usr.bin/cxxfilt/cxxfilt.c#L451-L456) (`if (sym[0] == '_' && is_mangled_prefix(sym + 1))`), so that double-underscored symbols like `__Z3foov` are only demangled when `-_` / `--strip-underscore` (`cfg->strip == STRIP_ONE`) is enabled.
  - **Evidence:** [`usr.bin/cxxfilt/cxxfilt.c:120, 149-156, 205-212, 434-456`](file:///home/segin/substrate/usr.bin/cxxfilt/cxxfilt.c#L434-L456).
  - **Verification:** Run `c++filt -n __Z3foov` and verify it outputs `__Z3foov` unchanged; run `c++filt -_ __Z3foov` and verify it outputs `foo()`.

- [ ] **[REQ-CXXF-TOK-001]** **WHEN** [`process_line()`](file:///home/segin/substrate/usr.bin/cxxfilt/cxxfilt.c#L462-L542) scans a line in `stream_mode`, it shall scan maximal contiguous identifier tokens (`is_token_char(line[i])`) from their starting boundary and pass each complete token to [`demangle_with_cfg()`](file:///home/segin/substrate/usr.bin/cxxfilt/cxxfilt.c#L406-L459), rather than testing `is_mangled_prefix(line + i)` at arbitrary interior character positions (`i > 0 && is_token_char(line[i - 1])`) and splitting identifiers like `FOO_Z3bar` into `FOObar` or `__Z3foov` under `-n` into `_foo()`.
  - **Evidence:** [`usr.bin/cxxfilt/cxxfilt.c:477-533`](file:///home/segin/substrate/usr.bin/cxxfilt/cxxfilt.c#L477-L533).
  - **Verification:** Pipe `FOO_Z3bar` and `__Z3foov` through `c++filt -n`; verify both are emitted unchanged (`FOO_Z3bar` and `__Z3foov`).

- [ ] **[REQ-CXXF-TOK-002]** **WHERE** `-_` / `--strip-underscore` (`cfg->strip == STRIP_ONE`) is active in [`process_line()`](file:///home/segin/substrate/usr.bin/cxxfilt/cxxfilt.c#L489-L532), **IF** a token starting with `__Z` / `__R` / `__D` fails to demangle (e.g., `__Zinvalid`), **THEN** `process_line()` shall emit the original unstripped token (`__Zinvalid`, starting at `consume_start`) rather than emitting `sym` starting at `demangle_start = i + 1` (`_Zinvalid`) and silently dropping the first underscore.
  - **Evidence:** [`usr.bin/cxxfilt/cxxfilt.c:458, 489-491, 506-532`](file:///home/segin/substrate/usr.bin/cxxfilt/cxxfilt.c#L489-L532).
  - **Verification:** Pipe `__Zinvalid` into `c++filt -_` and verify the output is `__Zinvalid`, not `_Zinvalid`.

- [ ] **[REQ-CXXF-TYPE-001]** **WHERE** `-t` / `--types` (`cfg->types != 0`) is enabled in `stream_mode`, [`process_line()`](file:///home/segin/substrate/usr.bin/cxxfilt/cxxfilt.c#L477-L533) shall attempt type demangling on identifier tokens even when they do not begin with `_Z`, `_R`, or `_D`, matching `argv_mode` behavior (`c++filt -t i` vs `echo i | c++filt -t`), and [`is_mangled_prefix()`](file:///home/segin/substrate/usr.bin/cxxfilt/cxxfilt.c#L344-L350) shall also recognize the `_d_` D-runtime prefix supported by [`demangle_auto()`](file:///home/segin/substrate/usr.lib/demangle/demangle.c#L81-L83).
  - **Evidence:** [`usr.bin/cxxfilt/cxxfilt.c:344-350, 487-499`](file:///home/segin/substrate/usr.bin/cxxfilt/cxxfilt.c#L344-L350), [`usr.lib/demangle/demangle.c:81-87`](file:///home/segin/substrate/usr.lib/demangle/demangle.c#L81-L87).
  - **Verification:** Run `echo PKc | c++filt -t` and `echo _d_allocmemory | c++filt` and verify they match `c++filt -t PKc` and `c++filt _d_allocmemory`.

- [ ] **[REQ-CXXF-CLI-001]** **WHEN** [`argv_mode()`](file:///home/segin/substrate/usr.bin/cxxfilt/cxxfilt.c#L591-L604) or [`process_line()`](file:///home/segin/substrate/usr.bin/cxxfilt/cxxfilt.c#L514-L539) frees buffers returned by [`demangle_with_cfg()`](file:///home/segin/substrate/usr.bin/cxxfilt/cxxfilt.c#L406-L459), it shall use [`demangle_free()`](file:///home/segin/substrate/usr.lib/demangle/demangle.c#L173-L176) and check `stdout` write errors (`puts(demangled) == EOF` / `fflush(stdout) == EOF`), returning exit status `1` on write failure.
  - **Evidence:** [`usr.bin/cxxfilt/cxxfilt.c:519, 523, 535, 599-600`](file:///home/segin/substrate/usr.bin/cxxfilt/cxxfilt.c#L519).
  - **Verification:** Run `c++filt _Z3foov >/dev/full` and verify `c++filt` exits with status `1`.

---

## 10. Manual Page & Build System Standards (`usr.bin/nm`, `usr.bin/ar`, `usr.bin/addr2line`, `usr.bin/cxxfilt`, `usr.lib/demangle`)

- [ ] **[REQ-MAN-001]** Per `AGENTS.md` Directive 9, [`usr.bin/nm/nm.1`](file:///home/segin/substrate/usr.bin/nm/nm.1), [`usr.bin/addr2line/addr2line.1`](file:///home/segin/substrate/usr.bin/addr2line/addr2line.1), and [`usr.bin/cxxfilt/c++filt.1`](file:///home/segin/substrate/usr.bin/cxxfilt/c++filt.1) shall be moved or installed into [`usr.man/man1/`](file:///home/segin/substrate/usr.man/man1), and the `install` targets in [`usr.bin/nm/Makefile:63-64`](file:///home/segin/substrate/usr.bin/nm/Makefile#L63-L64), [`usr.bin/ar/Makefile:28-31`](file:///home/segin/substrate/usr.bin/ar/Makefile#L28-L31), [`usr.bin/addr2line/Makefile:35-36`](file:///home/segin/substrate/usr.bin/addr2line/Makefile#L35-L36), [`usr.bin/cxxfilt/Makefile:48-50`](file:///home/segin/substrate/usr.bin/cxxfilt/Makefile#L48-L50), and [`usr.lib/demangle/Makefile:77, 80`](file:///home/segin/substrate/usr.lib/demangle/Makefile#L77-L80) shall install manual pages to `$(DESTDIR)/usr/share/man/man<section>` rather than `$(DESTDIR)/usr/man/man<section>`.
  - **Evidence:** [`usr.bin/nm/Makefile:63-64`](file:///home/segin/substrate/usr.bin/nm/Makefile#L63-L64), [`usr.bin/ar/Makefile:28-31`](file:///home/segin/substrate/usr.bin/ar/Makefile#L28-L31), [`usr.bin/addr2line/Makefile:35-36`](file:///home/segin/substrate/usr.bin/addr2line/Makefile#L35-L36), [`usr.bin/cxxfilt/Makefile:48-50`](file:///home/segin/substrate/usr.bin/cxxfilt/Makefile#L48-L50), [`usr.lib/demangle/Makefile:77, 80`](file:///home/segin/substrate/usr.lib/demangle/Makefile#L77-L80).
  - **Verification:** Run `make install DESTDIR=/tmp/stage` across `usr.man`, `usr.bin/{nm,ar,addr2line,cxxfilt}`, and `usr.lib/demangle`; verify all manual pages land under `/tmp/stage/usr/share/man/man{1,3}/`.

- [ ] **[REQ-MAN-002]** [`usr.bin/nm/nm.1:64-66`](file:///home/segin/substrate/usr.bin/nm/nm.1#L64-L66) shall be updated to remove the stale `"(not yet implemented)"` note from `-l` (which is implemented via `elf_dwarf_get_line_info()` at [`usr.bin/nm/nm.c:473-480, 509-516, 573-580`](file:///home/segin/substrate/usr.bin/nm/nm.c#L473-L480)).
  - **Evidence:** [`usr.bin/nm/nm.1:64-66`](file:///home/segin/substrate/usr.bin/nm/nm.1#L64-L66), [`usr.bin/nm/nm.c:473-480`](file:///home/segin/substrate/usr.bin/nm/nm.c#L473-L480).
  - **Verification:** Inspect `nm.1` and verify `-l` / `--line-numbers` documentation matches implementation.
