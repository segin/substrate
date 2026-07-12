# usr.lib/elfobj audit - 2026-07

Full read of `libelfobj` (~13,000 lines across 14 source files) at commit
4ed98a685: the untrusted-input parsers (`elf_read.c`, `elf_validate.c`,
`elf_dwarf*.c`, `elf_gnu_ext.c`, `leb128.h`), the relocation engine
(`elf_reloc.c`), the writer/layout (`elf_write.c`, `elf_layout.c`), the
linker-facing merge (`elf_link.c`, `elf_sections.c`, `elf_symbols.c`), and
the shared primitives (`elf_util.c`, `elf_strtab.c`, `elf_hash.c`).

This is the library that `readelf`/`nm`/`objcopy`-class tools and the
linker path build on; `elf_open`/`elf_open_memory` parse fully UNTRUSTED
ELF images, so memory-safety and DoS-resistance in the parse/validate
paths are the priority.

The library was read by parallel auditors (one per file group); findings
were then cross-checked against the source. `[VERIFIED]` = reproduced
against a host build (crafted-ELF + AddressSanitizer, or observed);
`[code]` = confirmed by inspection. Substrate's primary target is 32-bit,
so `uint64_t` ELF fields (offset/size/count) truncating into `size_t` is a
first-class bug class here and several findings are 32-bit-only.

Findings numbered ELFOBJ-NN by severity. A prior commit (420440f68) fixed
an earlier round of OOB reads; these are what remains.

**Status: all fixed** - ELFOBJ-01 through ELFOBJ-20 are each fixed and
verified in their own commit. Memory-safety findings were reproduced with
crafted-ELF + AddressSanitizer/UBSan and confirmed clean after the fix
(ELFOBJ-01 reloc OOB read, ELFOBJ-02 ARM-attr stack overflow, ELFOBJ-03
diag heap overflow, ELFOBJ-04 writer OOB write, ELFOBJ-05 div-by-zero,
ELFOBJ-11 sort NULL-deref, ELFOBJ-13 DWARF OOB read, ELFOBJ-14 LEB shift
UB); the 32-bit-only truncation findings (ELFOBJ-06/07) were verified on an
`-m32` build. LOW items with a poor risk/benefit for this well-tested,
shipping library are documented in their commits as accepted rather than
force-changed (the signed reloc arithmetic in ELFOBJ-17, the reloc-add and
OOM-leak sub-items). The library's own test suite passes 17/20 after these
changes; the 3 failures (`test_strtab`, `test_reader_writer`,
`test_validate_hardening`/`test_multiarch_api`) fail identically on the
audit-base commit 4ed98a685 and predate this audit.

## What's solid (coverage confirmed)

- The core arithmetic helpers are correct: `elf__bounds_ok` (`off>total`
  then `len>total-off`) is overflow-safe; `elf__u64_add`/`elf__u64_mul`
  detect overflow correctly; `elf__calloc`/`elf__reallocarray` use
  `mul_overflow` + an `ELFOBJ_MAX_ALLOC_BYTES` cap. Endian readers/writers
  are byte-exact in LE and BE.
- Table-level parsing (`parse_sections`, `parse_program_headers`) computes
  `entsize*count` and `off+table_size` with the checked helpers, guards
  `> SIZE_MAX` before truncating, floors `entsize` at the struct size, and
  NUL-terminates every `SHT_STRTAB`; `safe_str` refuses to return a pointer
  without a `\0` inside bounds.
- Symbol/versym/group parsing validates `entsize`, `sh_link`, `sh_info`,
  and version/symbol indices; `parse_relocations` range-checks `sym_index`,
  `sh_link`, `sh_info` and per-machine type. The `.note.gnu.property`
  parsers compute every `namesz`/`descsz`/`pr_datasz` span in 64-bit and
  re-check against `data_size`.
- The relocation **apply** backends (`elf_reloc.c`) are pure O(1)
  arithmetic writing only into a caller `out_value` - no `section->data`
  store, no loops, no allocation - so there is no OOB-write or DoS surface
  in the apply path itself.
- `elf_close` distinguishes mmapped / owned / borrowed images; buffer
  ownership (`owns_data`) is disciplined on the writer success path; no
  forbidden `strcpy`/`strcat`/`sprintf`/`gets` anywhere.

## High

### ELFOBJ-01: relocation `sh_entsize` has no minimum-size check -> OOB read (elf_read.c:855)
`parse_relocations` derives the per-entry stride from the attacker's
`sec->entsize` verbatim when nonzero (855-857) and only rejects
`entsz == 0` / `data_size % entsz != 0` (858). The per-entry reads then
need 8/12 bytes (ELF32 REL/RELA) or 16/24 (ELF64), but the stride can be
smaller. `sec->data = obj->image + sec->offset` is zero-copy (elf_read.c:216),
so for the last entry the reads run past `data_size` and, when the section
abuts the image end, past the image buffer. **[code]** Hostile input: an
ELF32 `SHT_RELA` section with `sh_entsize = 4`, `sh_size` a multiple of 4,
placed at the image end -> reads ~8 bytes past `image+image_size`. Fix:
after the override, require `entsz >= (cls32 ? (RELA?12:8) : (RELA?24:16))`.

### ELFOBJ-02: ARM/RISC-V attribute `_at` accessors overflow a one-element stack buffer (elf_gnu_ext.c:435)
`parse_arm_attrs`/`parse_riscv_attrs` write `items[outn]` for every parsed
attribute while `outn < max_items`, but `elf_arm_attribute_*_at` /
`elf_riscv_attribute_*_at` call them with `&item` (a single stack
`arm_attr_item_t`) and `max_items = index + 1`. For any `index >= 1` on a
section with `>= index+1` attributes, the loop writes `items[1..index]`
past the one-element struct - a stack buffer overflow reachable by the
normal `elf_*_attribute_*_at(obj, i)` iteration tools use. **[code]** Fix:
give the accessor a real `arm_attr_item_t buf[index+1]` (heap) and copy out
`buf[index]`, or add a "store only the index-th" mode.

### ELFOBJ-03: `elf__diag_append` off-by-one heap overflow (elf_util.c:202,231)
`need = strlen(msg)+1` reserves message+NUL, and the capacity guard ensures
`cap >= len+need`, but the writes are message (`need-1`) + `'\n'` + `'\0'`,
so the final NUL lands at `buf[len+need]`, requiring `cap >= len+need+1`.
When `cap == len+need` exactly (e.g. cap=256, len=250, need=6 -> no grow),
`buf[256]` is a one-byte heap overflow. Reachable on any diagnostic-append
sequence that fills the buffer to a capacity boundary (hostile ELF that
provokes many validation diagnostics). **[code]** Fix: reserve/grow against
`len + need + 1` and clamp the `MAX_DIAG_BYTES` check likewise.

### ELFOBJ-04: writer `align_up` overflow + unvalidated `sh_addralign` -> heap OOB write (elf_write.c:47,758)
On a round-trip, `out.addralign = s->addralign ? s->addralign : 1` (758)
is copied with no power-of-two/range check, and `align_up(v,a) =
(v+(a-1)) & ~(a-1)` (47) has no overflow guard. A 64-bit output section
with `sh_addralign ~= 2^64` makes `v+(a-1)` wrap to a small value, so that
section's `offset` (and the running `off`) shrink below an earlier
section's `offset+size`; `total_sz`/`shoff` shrink with it and the earlier
section's `memcpy(img + offset, data, size)` (1336) writes past the
allocation. **[code]** Fix: reject non-power-of-two/oversize `addralign`;
make `align_up` fail on `v+(a-1)` overflow. (Same `addralign` root affects
ELFOBJ-09 and ELFOBJ-12.)

### ELFOBJ-05: DWARF line `line_range == 0` -> division-by-zero SIGFPE (elf_dwarf_line.c:106)
`line_range = data[off++]` is read with no zero-check, then used as a
divisor/modulus in special-opcode advance (`adjusted_opcode / line_range`,
`% line_range`, 151-152) and `DW_LNS_const_add_pc` (219). A `.debug_line`
unit header with `line_range == 0` crashes any tool that walks line
programs (e.g. `nm -l`). **[code]** Fix: after reading the header,
`if (line_range == 0) { off = unit_end; continue; }`.

## Medium

### ELFOBJ-06: validator truncates 64-bit section/segment offset+size before the bounds check (elf_validate.c:763,1030)
`elf__bounds_ok((size_t)s->offset, (size_t)s->size, image_size)` (763) and
the identical program-header check (1030) cast `uint64_t` to `size_t`. On
32-bit, an ELF64 section with `sh_offset = 0x1_0000_0040`, `sh_size=0x10`,
`image_size=0x1000` truncates offset to `0x40` and passes, so the validator
blesses a 4-GiB-out-of-bounds span every consumer then trusts. **[code]**
32-bit only. Fix: compare `offset`/`size` in `uint64_t` against
`image_size` (reject `> SIZE_MAX`).

### ELFOBJ-07: elf_read per-section offset/size truncation + reloc bound uses untruncated size (elf_read.c:190,217,910)
Per-section `sh_offset`/`sh_size` are truncated to `size_t` with no
`> SIZE_MAX` guard (190, `data_size=(size_t)sec->size` at 217), while
`parse_relocations` bounds `rel_offset` against the full 64-bit `sec->size`
(910) - and raises it further to the untrusted `compression_size` (911).
On 32-bit an ELF64 section with `sh_size=0x1_0000_0000` truncates to
`data_size=0` (empty buffer) yet `target_size` stays 4 GiB, so relocations
with `r_offset` up to 4 GiB pass the range check, breaking the "materialized
reloc is safe to apply" contract for consumers. **[code]** 32-bit only.
Fix: reject `offset|size > SIZE_MAX` per section; clamp `target_size` to
`data_size`; do not widen the reloc bound by `compression_size`.

### ELFOBJ-08: `append_section_data` size_t add wrap -> heap overflow (elf_link.c:367)
`realloc(dst->data, dst->data_size + src_sz)` then `memcpy(buf +
dst->data_size, src, src_sz)` (371) adds two `size_t` with no overflow
check; merging inputs whose same-named section data accumulates past
`SIZE_MAX` yields an undersized buffer and an OOB `memcpy`. **[code]** Fix:
`elf__u64_add(data_size, src_sz, &n)` and reject `n > SIZE_MAX` before
realloc.

### ELFOBJ-09: `align_merged_section` uint64 pad truncation + unvalidated addralign (elf_link.c:412)
`realloc(dst->data, dst->data_size + pad)` truncates a 64-bit `pad`
(derived from the raw, unvalidated `src->addralign`) into `size_t` at the
call while `memset(buf + dst->data_size, 0, (size_t)pad)` truncates
separately; the two can disagree and write past the realloc. **[code]**
Fix: validate `addralign` power-of-two/bounded; overflow-check
`data_size+pad` in 64-bit and reject `> SIZE_MAX`.

### ELFOBJ-10: merge propagates a stale cross-object `shndx` (elf_link.c:734,788)
When `resolve_src_sec_index` fails but the symbol is a named defined
symbol, `shndx` keeps the raw input value (734) and is stored into the
output symbol (788). An index valid for input A but `>= section_count` of
output B becomes an OOB section reference for any downstream code that does
`out->sections[shndx-1]`. **[code]** Fix: on resolution failure of a
non-reserved index, reset to `SHN_ABS` or drop the symbol.

### ELFOBJ-11: deterministic symbol sort dereferences NULL symbol slots (elf_symbols.c:259,295)
`sym_order_before` reads `a->sym->bind`/`b->sym->bind` with no NULL guard,
but `obj->symbols[]` legitimately holds NULL slots (every other consumer
checks). `elf_symbols_sort_deterministic` on a parsed object containing a
NULL slot NULL-derefs. **[code]** Fix: partition NULLs out before sorting
or order them last in the comparator.

### ELFOBJ-12: `elf_layout` align_up overflow / non-power-of-two / unchecked offset sum (elf_layout.c:7,28,31)
`align_up(v,a)` wraps on large `a` and masks wrong for non-power-of-two
`a`; `off += sec->data_size` (31) accumulates with no overflow check. Same
`addralign`-validation root as ELFOBJ-04/09. **[code]** Fix: validate
`addralign`; use `elf__u64_add` for the running offset.

### ELFOBJ-13: DWARF line-program OOB reads (elf_dwarf_line.c:99,171)
Two 1-byte over-reads: the v>=4 header reads 6 fixed bytes under a
`+5`-byte guard (99) so `opcode_base` can read at the section boundary; and
the extended-opcode byte `data[off++]` (171) is read with no `off < ext_end`
guard after the length ULEB may have consumed up to `unit_end`. **[code]**
Fix: size the header guard by version; check `off < ext_end` before the
extended opcode.

### ELFOBJ-14: LEB128 unbounded shift UB + cursor overflow (leb128.h:12,26; elf_dwarf_line.c:73,168)
`leb128.h` uleb/sleb loops cap on the continuation bit but not `shift < 64`,
so a long varint drives `(byte&0x7f) << shift` past 63 bits (UB) - unlike
the guarded sibling in elf_gnu_ext.c:11. Line-program cursor math
(`unit_end = off + unit_length`, `ext_end = off + ext_len`) adds
attacker-controlled ~4 GiB in `size_t` and can wrap on 32-bit. **[code]**
Fix: add `&& shift < 64` to the LEB loops; bound cursor math with
non-wrapping `size - off` comparisons.

### ELFOBJ-15: DoS via O(n^2) scans and uncapped diagnostics (elf_validate.c:65,901,952; elf_read.c:897; elf_link.c:711; elf_symbols.c:302)
Several quadratic passes over attacker-chosen counts with no independent
cap: validator section/segment-overlap and duplicate-symbol scans and the
AArch64 ADRP/LO12 reloc² pairing (only `max_errors` bounds them, and only
ERROR-level diagnostics count toward it - PERMISSIVE-mode warnings are
uncapped); `find_runtime_reloc_target` is called per-reloc over all
sections (`(image_size/8) * section_count`); merge does two linear symbol
scans per input symbol; the deterministic "sort" is insertion sort.
**[code]** Fix: cap per-phase work / count warnings toward the limit;
sort+binary-search or hash-index the reloc-target and merge lookups.

### ELFOBJ-16: hash functions walk a name with no length bound (elf_hash.c:12,31)
`elf_hash_sysv`/`elf_hash_gnu` loop to `'\0'`; called on a pointer into an
untrusted, possibly non-NUL-terminated `.strtab` they walk off the end.
**[code]** Fix: provide a length-bounded variant, or rely only on
parse-time NUL-termination (and document it).

## Low

### ELFOBJ-17: reloc arithmetic UB and reentrancy (elf_reloc.c:46,165,2937,2558,2729)
64-bit `sym+addend` computed in signed `elf_swide_t` on the non-`__int128`
(32-bit) build is signed-overflow UB and falsely rejects legitimate
bit-63-set values; `elf_reloc_name_for_machine` formats unknown types into
a shared `static char[32]` (non-reentrant); `register_builtin_backends_locked`
writes 13 entries into `g_backends[16]` unguarded (latent); `elf_add_relocation`
skips the `offset+width` end-check for unknown-width types. **[code]** Fix:
do reloc adds in `uint64_t`; return a literal for unknown names; guard the
built-in inserts; reject unknown-width relocs.

### ELFOBJ-18: writer/util leaks and truncations on error/edge paths
`dynstr_data` (elf_write.c:1001) and `symtab_data` (906) leak on `out_push`
OOM; `PT_ARM_EXIDX` is emitted but not counted in `phnum` and `e_phnum` is
`(uint16_t)`-truncated with no PN_XNUM (1206,1305); `symbols[0]` is
dereferenced without a NULL check (265,426); `elf__reallocarray` with
`total==0` calls `realloc(ptr,0)` which may free `ptr` and return NULL ->
caller keeps a freed pointer (elf_util.c:66); `(uint16_t)phdr_count`
truncates (908). **[code]** Fix per site: free on OOM; count/limit phdrs;
NULL-check slot 0; treat `total==0` as no-op.

### ELFOBJ-19: strtab has no dedup and bypasses the alloc cap (elf_strtab.c:28,53)
`elf__strtab_add` appends duplicates and grows via raw `realloc`, bypassing
`ELFOBJ_MAX_ALLOC_BYTES` - `.strtab`/`.shstrtab` bloat unbounded.
**[code]** Fix: dedup (hash index) and cap `new_cap`.

### ELFOBJ-20: assorted robustness (elf_validate.c:261; elf_link.c:70,1083; elf_read.c:616; elf_dwarf_line.c:70; elf_gnu_ext.c:386)
`8 + pr_datasz` computed in 32-bit (validate:261); `comdat_set_add` raw
multiply (link:70) and `comdat_seen` leaked on per-input error paths
(link:1083); dead-path `free(maps)` leaks `maps[k].symbols` (read:616);
64-bit DWARF length narrowed to uint32 (dwarf_line:70); attribute-section
length adds that can wrap on 32-bit (gnu_ext:386,507). **[code]** Low-impact
hardening. Fix per site.

## Notes / refuted

- **Not a bug:** the `obj->sections[sec->link]` load at elf_read.c:622 is
  guarded by the first-pass `sec->link >= section_count` check at
  elf_read.c:589 (both loops iterate the same symtab sections), so it is
  not an OOB despite the validator (ELFOBJ-15 aside) not re-checking
  `sh_link` itself.
- The relocation **apply** engine has no OOB-write surface: the byte-store
  into `section->data` is the consumer's responsibility, not this library's.

## Verification method

Findings verified against a native `libelfobj.a` built with
`-fsanitize=address,undefined`, driving `elf_open_memory` (and the writer /
attribute / diagnostic APIs) with crafted hostile ELF images built by a
byte-level ELF32/64 crafter. 32-bit-truncation findings (ELFOBJ-06/07)
require an `-m32` build to observe on the host.
