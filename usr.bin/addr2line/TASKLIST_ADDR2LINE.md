# `usr.bin/addr2line` Tasklist

Goal: implement `addr2line` using `libelfobj` for ELF loading and raw
`.debug_*` section access, with an internal DWARF line‑program decoder.

> **Dependencies:**
>
> | Library          | Usage                                          |
> |------------------|------------------------------------------------|
> | `libelfobj.a`    | ELF open/close, section lookup, symbol lookup  |
> | `libdemangle.a`  | C++/Rust/D-lang name demangling (`-C` flag)    |

---

## Developer Stories

> **DS‑1.** As a *kernel developer*, I want to translate a panic address
> into a source filename and line number, so that I can locate the
> faulting code without a debugger.

> **DS‑2.** As a *toolchain developer*, I want addr2line to consume
> DWARF v2–v5 debug info exclusively through `libelfobj` section access,
> so that ELF parsing is consistent across all binutils.

> **DS‑3.** As a *C++ developer*, I want demangled function names in
> addr2line output, so that I can read template‑heavy backtraces.

> **DS‑4.** As an *application developer*, I want addr2line to unwind
> inline frames, so that optimized builds still produce useful
> stack traces.

> **DS‑5.** As a *shared‑library author*, I want addr2line to handle
> PIE and ET_DYN objects with correct load‑bias adjustment, so that
> ASLR‑randomized crash addresses resolve correctly.

> **DS‑6.** As a *build engineer*, I want addr2line to transparently
> decompress `SHF_COMPRESSED` / `.zdebug_*` sections, so that
> compressed‑debug builds work without extra tooling.

> **DS‑7.** As a *CI system*, I want addr2line to exit 0 even when
> addresses cannot be resolved, so that scripts consuming its output
> do not fail spuriously.

---

## Requirements (INCOSE/EARS)

### Functional — ELF Loading

| ID       | Requirement |
|----------|-------------|
| **FR‑01** | The system shall open an ELF file specified by the `-e` flag using `elf_open()`. |
| **FR‑02** | When no `-e` flag is provided, the system shall default to opening `a.out`. |
| **FR‑03** | The system shall locate `.debug_line`, `.debug_info`, `.debug_abbrev`, `.debug_str`, `.debug_line_str`, `.debug_ranges`, and `.debug_rnglists` sections via `elf_section_find_by_name()`. |
| **FR‑04** | Where a `.debug_*` section has the `SHF_COMPRESSED` flag or a `.zdebug_*` name, the system shall transparently decompress its contents before parsing. |
| **FR‑05** | The system shall read `.symtab` and `.dynsym` for fallback function‑name lookup when DWARF info is absent. |
| **FR‑06** | The system shall call `elf_close()` on every opened handle, including on all error paths. |

### Functional — DWARF Line‑Program Decoder

| ID       | Requirement |
|----------|-------------|
| **FR‑10** | The system shall parse DWARF line‑number program headers for versions 2, 3, 4, and 5. |
| **FR‑11** | The system shall decode all standard opcodes: `DW_LNS_copy`, `DW_LNS_advance_pc`, `DW_LNS_advance_line`, `DW_LNS_set_file`, `DW_LNS_set_column`, `DW_LNS_negate_stmt`, `DW_LNS_set_basic_block`, `DW_LNS_const_add_pc`, `DW_LNS_fixed_advance_pc`, `DW_LNS_set_prologue_end`, `DW_LNS_set_epilogue_begin`, `DW_LNS_set_isa`. |
| **FR‑12** | The system shall decode extended opcodes: `DW_LNE_end_sequence`, `DW_LNE_set_address`, `DW_LNE_define_file`, `DW_LNE_set_discriminator`. |
| **FR‑13** | When an unknown extended opcode is encountered, the system shall skip it using the length prefix without error. |
| **FR‑14** | The system shall decode special opcodes to simultaneously advance address and line. |
| **FR‑15** | The system shall decode unsigned and signed LEB128 values with overflow guards (reject values exceeding 64 bits). |

### Functional — Address Resolution

| ID       | Requirement |
|----------|-------------|
| **FR‑20** | The system shall build a sorted line table from decoded rows and perform binary search to resolve an address to the closest preceding `file:line` entry. |
| **FR‑21** | The system shall iterate all compilation‑unit headers in `.debug_line` to cover the full address space. |
| **FR‑22** | The system shall resolve file indices to `directory/filename` strings. |
| **FR‑23** | When no line entry matches a query address, the system shall output `??:0`. |
| **FR‑24** | When the `-f` flag is active, the system shall scan `.debug_info` for `DW_TAG_subprogram` entries and match the query address to the enclosing subprogram's `DW_AT_low_pc`/`DW_AT_high_pc` or `DW_AT_ranges`. |
| **FR‑25** | If no `DW_TAG_subprogram` matches, the system shall fall back to the nearest `STT_FUNC` symbol in `.symtab` where `value ≤ query < value + size`. |
| **FR‑26** | When no function name is found, the system shall output `??`. |

### Functional — Inline Frames

| ID       | Requirement |
|----------|-------------|
| **FR‑30** | When the `-i` flag is active, the system shall parse `DW_TAG_inlined_subroutine` entries and resolve `DW_AT_call_file`, `DW_AT_call_line`, and `DW_AT_abstract_origin`. |
| **FR‑31** | The system shall print inline frames from innermost to outermost, one line per frame. |

### Functional — Address Adjustment

| ID       | Requirement |
|----------|-------------|
| **FR‑40** | When the `-j <section>` flag is provided, the system shall treat input addresses as offsets within the named section and add the section's `sh_addr` before lookup. |
| **FR‑41** | When the input file is `ET_DYN` (PIE/shared object), the system shall apply load‑bias correction to resolve absolute runtime addresses. |

### Functional — Flags and I/O

| ID       | Requirement |
|----------|-------------|
| **FR‑50** | The system shall accept addresses as command‑line arguments after all flags. |
| **FR‑51** | When no address arguments are given, the system shall read addresses from stdin, one per line. |
| **FR‑52** | The `-s` flag shall cause the system to strip directory prefixes, printing only the base filename. |
| **FR‑53** | The `-C` flag shall cause the system to demangle function names by calling `demangle()` from `libdemangle` with `DEMANGLE_AUTO`. |
| **FR‑54** | The `-p` flag shall cause the system to print each result on one line in `function at file:line` format. |
| **FR‑55** | The `-a` flag shall cause the system to print the query address before each result. |
| **FR‑56** | The `--help` flag shall print usage information and exit 0. |
| **FR‑57** | The `--version` flag shall print version information and exit 0. |

### Non‑Functional — Robustness

| ID       | Requirement |
|----------|-------------|
| **NF‑01** | If the input file is not a valid ELF, then the system shall print a diagnostic to stderr and exit 1. |
| **NF‑02** | If `.debug_line` is absent, then the system shall output `??:0` for every query address and exit 0. |
| **NF‑03** | If DWARF data is malformed, then the system shall output `??:0` for the affected address, warn on stderr, and continue processing remaining addresses. |
| **NF‑04** | If an input address is not a valid hexadecimal value, then the system shall warn on stderr, output `??:0`, and continue. |
| **NF‑05** | If the `-e` file cannot be opened (not found, permission denied), then the system shall print a diagnostic to stderr and exit 1. |
| **NF‑06** | The system shall process up to 64 KiB DWARF line sections without allocation failure. |
| **NF‑07** | The system shall guard against DWARF recursion or circular references exceeding 256 nesting levels. |

---

## 1. `libelfobj` Integration

- [x] Open input file with `elf_open()` / `elf_open_with_options()`. **(FR‑01, FR‑02)**
- [x] Locate `.debug_line` section via `elf_section_find_by_name()`. **(FR‑03)**
- [x] Locate `.debug_info` section (for compilation‑unit base addresses). **(FR‑03)**
- [x] Locate `.debug_abbrev` section (for abbreviation table lookup). **(FR‑03)**
- [x] Locate `.debug_str` / `.debug_line_str` sections (for string offsets). **(FR‑03)**
- [x] Locate `.debug_ranges` / `.debug_rnglists` sections (for address ranges). **(FR‑03)**
- [x] Detect compressed debug sections (`.zdebug_*` / `SHF_COMPRESSED`) via `elf_section_is_compressed_debug()` and handle `SHT_COMPRESSED` header parsing. **(FR‑04)**
- [x] Read `.symtab` / `.dynsym` for fallback function‑name lookup when DWARF is absent. **(FR‑05)**
- [x] Call `elf_close()` on every opened handle; no leaks on error paths. **(FR‑06)**
- [x] Handle `elf_open()` failure: print diagnostic, exit 1. **(NF‑01)**

---

## 2. DWARF Line‑Program Decoder

### 2a. Line Number Header (DWARF v2–v5) **(FR‑10)**
- [x] Parse `unit_length`, `version`, `header_length`, `minimum_instruction_length`, `maximum_operations_per_instruction`.
- [x] Parse `default_is_stmt`, `line_base`, `line_range`, `opcode_base`.
- [x] Parse standard opcode length table.
- [x] Parse include directories table (v4: null‑terminated list; v5: directory entry format).
- [x] Parse file name table (v4: null‑terminated entries; v5: file name entry format).
- [x] Validate header fields against `.debug_line` section bounds.

### 2b. Standard Opcodes **(FR‑11)**
- [x] `DW_LNS_copy` — append row, reset discriminator.
- [x] `DW_LNS_advance_pc` — advance `address` by operand × `min_insn_length`.
- [x] `DW_LNS_advance_line` — add signed LEB128 to `line`.
- [x] `DW_LNS_set_file` — update `file`.
- [x] `DW_LNS_set_column` — update `column`.
- [x] `DW_LNS_negate_stmt` — toggle `is_stmt`.
- [x] `DW_LNS_set_basic_block`.
- [x] `DW_LNS_const_add_pc` — special advance with opcode 255.
- [x] `DW_LNS_fixed_advance_pc` — advance by fixed uhalf.
- [x] `DW_LNS_set_prologue_end` (DWARF v3+).
- [x] `DW_LNS_set_epilogue_begin` (DWARF v3+).
- [x] `DW_LNS_set_isa` (DWARF v3+).

### 2c. Extended Opcodes **(FR‑12, FR‑13)**
- [x] `DW_LNE_end_sequence` — mark `end_sequence`, append row, reset state.
- [x] `DW_LNE_set_address` — set `address` to relocatable value.
- [x] `DW_LNE_define_file` — add file entry (deprecated in v5).
- [x] `DW_LNE_set_discriminator` (DWARF v4+).
- [x] Skip unknown extended opcodes gracefully using length prefix.

### 2d. Special Opcodes **(FR‑14)**
- [x] Decode `adjusted_opcode = opcode - opcode_base`.
- [x] Advance address: `adjusted_opcode / line_range * min_insn_length`.
- [x] Advance line: `line_base + (adjusted_opcode % line_range)`.
- [x] Append row.

### 2e. LEB128 Encoding **(FR‑15)**
- [x] Unsigned LEB128 decoder with overflow guard.
- [x] Signed LEB128 decoder with overflow guard.

---

## 3. Address‑to‑Line Lookup

- [x] Build sorted line‑table from decoded rows (address → file:line pairs). **(FR‑20)**
- [x] Binary search for the closest row with `address ≤ query ≤ next_address`. **(FR‑20)**
- [x] Handle multiple compilation units: iterate all `.debug_line` unit headers. **(FR‑21)**
- [x] Resolve file index to directory + filename string. **(FR‑22)**
- [x] Output format: `filename:line` (or `filename:line:column` with `-c`). **(FR‑22)**
- [x] If no line entry found: output `??:0`. **(FR‑23)**

---

## 4. Function Name Lookup

- [x] Scan `.debug_info` for `DW_TAG_subprogram` entries with `DW_AT_low_pc` / `DW_AT_high_pc` or `DW_AT_ranges`. **(FR‑24)**
- [x] Match query address to enclosing subprogram range. **(FR‑24)**
- [x] Read `DW_AT_name` (or `DW_AT_linkage_name`) for function name. **(FR‑24)**
- [x] Fallback: if no DWARF info, use `.symtab` symbols — find nearest `STT_FUNC` with `value ≤ query < value + size`. **(FR‑25)**
- [x] If no function found: output `??`. **(FR‑26)**

---

## 5. Inlined Frames (`-i`)

- [x] Parse `DW_TAG_inlined_subroutine` entries in `.debug_info`. **(FR‑30)**
- [x] Read `DW_AT_call_file`, `DW_AT_call_line`, `DW_AT_call_column`. **(FR‑30)**
- [x] Read `DW_AT_abstract_origin` → resolve to `DW_TAG_subprogram` name. **(FR‑30)**
- [x] For each query address, walk the inline chain from innermost to outermost. **(FR‑31)**
- [x] Print one line per frame (innermost first). **(FR‑31)**

---

## 6. Address Adjustment

- [x] Default: treat addresses as absolute virtual addresses.
- [x] `-j <section>` / `--section=<section>`: treat addresses as offsets within named section; add section's `sh_addr` before lookup. **(FR‑40)**
- [x] Auto‑detect PIE (`ET_DYN`): if input is `ET_DYN` and addresses look load‑relative, apply load bias. **(FR‑41)**
- [x] `--basenames`: strip directory prefix, print only filename. **(FR‑52)**

---

## 7. Flags

- [x] `-e <file>` / `--exe=<file>`: specify ELF executable/object (default: `a.out`). **(FR‑01, FR‑02)**
- [x] `-f` / `--functions`: display function name before file:line. **(FR‑24)**
- [x] `-s` / `--basenames`: strip directory prefixes from filenames. **(FR‑52)**
- [x] `-i` / `--inlines`: unwind inline frames (see §5). **(FR‑30, FR‑31)**
- [x] `-j <section>` / `--section=<section>`: interpret addresses relative to section. **(FR‑40)**
- [x] `-C` / `--demangle`: demangle function names via `demangle()` from `libdemangle`. **(FR‑53)**
- [x] `-p` / `--pretty-print`: print each frame on one line with ` at file:line` format. **(FR‑54)**
- [x] `-a` / `--addresses`: print address before each result. **(FR‑55)**
- [x] `--help` / `-h`: usage. **(FR‑56)**
- [x] `--version` / `-V`: version. **(FR‑57)**
- [x] Addresses on stdin (one per line) when no arguments after flags. **(FR‑51)**
- [x] Addresses on command line after the flags. **(FR‑50)**

---

## 8. Error Handling

- [x] Non‑ELF input: `addr2line: <file>: file format not recognized`, exit 1. **(NF‑01)**
- [x] Missing `.debug_line`: all lookups return `??:0`, exit 0 (not an error). **(NF‑02)**
- [x] Malformed DWARF: print `??:0` for affected addresses, warn on stderr, continue. **(NF‑03)**
- [x] Invalid address format on input: warn on stderr, print `??:0`, continue. **(NF‑04)**
- [x] `-e` file not found: error message, exit 1. **(NF‑05)**
- [x] Permission denied: error message, exit 1. **(NF‑05)**

---

## 9. Build System

- [x] Create `Makefile` in `usr.bin/addr2line/`.
- [x] Link against `libelfobj.a` and `libdemangle.a`.
- [x] Include paths for `elfobj.h` and `demangle.h`.
- [x] `NATIVE_BUILD=1` support for host testing.
- [x] `install` target to `$(DESTDIR)/usr/bin/addr2line`.

---

## 10. Testing

### 10a. Basic Lookup Tests **(FR‑20, FR‑21, FR‑22, FR‑23)**
- [x] Known `ET_EXEC` ELF32 with DWARF v4: address → `file:line` verified.
- [x] Known `ET_EXEC` ELF64 with DWARF v4: address → `file:line` verified.
- [x] Known ELF with DWARF v5: address → `file:line` verified.
- [x] Multiple addresses: verify each resolved independently.

### 10b. Function Name Tests **(FR‑24, FR‑25, FR‑26, FR‑53)**
- [x] `-f`: verify function name printed before file:line.
- [x] DWARF `DW_AT_linkage_name`: verify mangled name shown without `-C`.
- [x] `-f -C`: verify demangled C++ function name.
- [x] Fallback to `.symtab` when `.debug_info` absent: verify function name.

### 10c. Inline Frame Tests **(FR‑30, FR‑31)**
- [ ] `-i`: verify multiple frames for inlined call site.
- [ ] Verify ordering: innermost frame first.
- [ ] Verify `DW_AT_call_file` / `DW_AT_call_line` values.

### 10d. Section Offset Tests **(FR‑40)**
- [ ] `-j .text`: addresses treated as section offsets.
- [ ] Invalid section name: error handling.

### 10e. PIE / Shared Object Tests **(FR‑41)**
- [ ] `ET_DYN` PIE binary: verify addresses resolved with load bias.
- [ ] `ET_DYN` shared library: verify symbol lookup.

### 10f. Edge Cases **(NF‑01 – NF‑07)**
- [ ] Stripped binary (no DWARF): all addresses return `??:0`, `??` for function.
- [ ] Address 0x0: valid lookup, return whatever is at address 0.
- [ ] Address beyond `.text` end: return `??:0`.
- [ ] Empty input (no addresses): no output.
- [ ] Very large DWARF (many CUs): no crash, correct lookup.

### 10g. Flag Combination Tests **(FR‑50 – FR‑55)**
- [ ] `-f -s`: function + basename only.
- [ ] `-f -C -i -p`: demangled, inlined, pretty‑print.
- [ ] `-a`: verify address column prepended.
- [ ] Stdin mode: pipe addresses, verify per-line output.

### 10h. Compressed Debug Tests **(FR‑04)**
- [ ] `.zdebug_line` (zlib‑gnu): verify transparent decompression.
- [ ] `SHF_COMPRESSED` `.debug_line`: verify transparent decompression.

### 10i. Integration Tests
- [ ] Compile `.c` → `a.out` with `-g`, run addr2line on known function address, verify output.
- [ ] Compare output against host `addr2line` on same binary.

---

## 11. Man Page

- [ ] Write `addr2line.1` covering all flags, address formats, and output modes.
- [ ] Document DWARF version support (v2–v5).
- [ ] Reference `demangle(3)` for demangling details.
- [ ] Install to `$(DESTDIR)/usr/share/man/man1/`.

---

## Traceability Matrix

| Requirement | Task(s)              | Test(s)          | Story  |
|-------------|----------------------|------------------|--------|
| FR‑01       | §1, §7               | 10b, 10i         | DS‑2   |
| FR‑02       | §7                   | 10i              | DS‑2   |
| FR‑03       | §1                   | 10a              | DS‑2   |
| FR‑04       | §1                   | 10h              | DS‑6   |
| FR‑05       | §1, §4               | 10b              | DS‑1   |
| FR‑06       | §1                   | 10f              | DS‑2   |
| FR‑10       | §2a                  | 10a              | DS‑1   |
| FR‑11       | §2b                  | 10a              | DS‑1   |
| FR‑12       | §2c                  | 10a              | DS‑1   |
| FR‑13       | §2c                  | 10f              | DS‑1   |
| FR‑14       | §2d                  | 10a              | DS‑1   |
| FR‑15       | §2e                  | 10f              | DS‑1   |
| FR‑20       | §3                   | 10a              | DS‑1   |
| FR‑21       | §3                   | 10a              | DS‑1   |
| FR‑22       | §3                   | 10a              | DS‑1   |
| FR‑23       | §3                   | 10f              | DS‑1   |
| FR‑24       | §4                   | 10b              | DS‑3   |
| FR‑25       | §4                   | 10b              | DS‑1   |
| FR‑26       | §4                   | 10b, 10f         | DS‑1   |
| FR‑30       | §5                   | 10c              | DS‑4   |
| FR‑31       | §5                   | 10c              | DS‑4   |
| FR‑40       | §6                   | 10d              | DS‑5   |
| FR‑41       | §6                   | 10e              | DS‑5   |
| FR‑50       | §7                   | 10g              | DS‑1   |
| FR‑51       | §7                   | 10g              | DS‑1   |
| FR‑52       | §6, §7               | 10g              | DS‑1   |
| FR‑53       | §7                   | 10b              | DS‑3   |
| FR‑54       | §7                   | 10g              | DS‑1   |
| FR‑55       | §7                   | 10g              | DS‑1   |
| FR‑56       | §7                   | —                | —      |
| FR‑57       | §7                   | —                | —      |
| NF‑01       | §1, §8               | 10f              | DS‑7   |
| NF‑02       | §8                   | 10f              | DS‑7   |
| NF‑03       | §8                   | 10f              | DS‑7   |
| NF‑04       | §8                   | 10f              | DS‑7   |
| NF‑05       | §8                   | 10f              | DS‑7   |
| NF‑06       | §2                   | 10f              | DS‑2   |
| NF‑07       | §2                   | 10f              | DS‑2   |
