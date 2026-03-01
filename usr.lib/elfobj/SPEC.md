# `libelfobj` — Specification

## 1. Purpose

`libelfobj` is a portable, zero-dependency C library for reading, creating, modifying, validating, and writing ELF (Executable and Linkable Format) object files. It serves as the sole ELF manipulation layer for all Substrate binutils (`as`, `ld`, `nm`, `objcopy`, `objdump`, `readelf`, `strip`, `size`, `strings`, `ar`, `addr2line`, `elfedit`).

## 2. Scope

| Attribute          | Value                                                        |
|--------------------|--------------------------------------------------------------|
| Library name       | `libelfobj`                                                  |
| Install path       | `/usr/lib/libelfobj.a` (static library)                      |
| Public header      | `<elfobj.h>`                                                 |
| ELF classes        | ELF32, ELF64                                                 |
| Byte orders        | Little-endian (ELFDATA2LSB), Big-endian (ELFDATA2MSB)        |
| Architectures      | i386 (EM_386), x86-64 (EM_X86_64), ARMv7 (EM_ARM), AArch64 (EM_AARCH64) |
| Host build         | `NATIVE_BUILD=1` for development/test on Linux/BSD host      |
| Dependencies       | None (freestanding C99; uses only libc for malloc/stdio)     |

## 3. Definitions

| Term              | Definition                                                                   |
|-------------------|------------------------------------------------------------------------------|
| EARS              | Easy Approach to Requirements Syntax (ISO/IEC/IEEE 29148 compatible)         |
| ET_REL            | Relocatable object file (`.o`)                                               |
| ET_EXEC           | Executable file                                                              |
| ET_DYN            | Shared object / PIE                                                          |
| ET_CORE           | Core dump file                                                               |
| Relocation        | A fixup record binding a symbol reference to its definition at link time     |
| REL               | Relocation without explicit addend (i386, ARM); addend in instruction        |
| RELA              | Relocation with explicit addend (x86-64, AArch64)                            |
| Section           | Named region of an ELF file with type, flags, and content                    |
| Segment           | Runtime-visible region described by a program header                         |
| Backend           | Architecture-specific relocation engine plug-in                              |
| Build attribute   | ARM EABI vendor-specific metadata tag in `.ARM.attributes`                   |
| GNU property      | Feature flag in `.note.gnu.property` (ISA level, BTI, PAC)                   |

---

## 4. Functional Requirements

### 4.1 ELF Reading

**REQ-ELF-010** *(Ubiquitous)*
The library shall parse ELF files from a file path via `elf_open_file()` or from a memory buffer via `elf_open_memory()`.

**REQ-ELF-011** *(Ubiquitous)*
The library shall parse both ELF32 and ELF64 file headers and expose class, endianness, machine type, entry point, and flags.

**REQ-ELF-012** *(Ubiquitous)*
The library shall parse section headers and provide indexed access to sections by name or by index.

**REQ-ELF-013** *(Ubiquitous)*
The library shall parse symbol tables (`.symtab`, `.dynsym`) and provide access to each symbol's name, value, size, type, binding, visibility, and section index.

**REQ-ELF-014** *(Ubiquitous)*
The library shall parse relocation sections (`SHT_REL`, `SHT_RELA`) and expose each relocation's offset, type, symbol reference, and addend.

**REQ-ELF-015** *(Ubiquitous)*
The library shall parse program headers and expose each segment's type, flags, offset, virtual address, physical address, file size, memory size, and alignment.

**REQ-ELF-016** *(Ubiquitous)*
The library shall parse string tables (`SHT_STRTAB`) with bounds checking on all index accesses.

**REQ-ELF-017** *(Event-driven)*
When the input file is truncated or contains invalid offsets, the library shall return an error code and set a diagnostic message without crashing.

**REQ-ELF-018** *(Ubiquitous)*
The library shall perform all multi-byte reads using the endianness specified in `e_ident[EI_DATA]`, supporting both little-endian and big-endian files.

**REQ-ELF-019** *(Event-driven)*
When `elf_open_file()` is called with a path that cannot be opened, the library shall return `ELF_ERR_IO` with a diagnostic message.

### 4.2 ELF Creation

**REQ-ELF-020** *(Ubiquitous)*
The library shall create new ELF objects via `elf_create()` with specified class, endianness, and machine type.

**REQ-ELF-021** *(Ubiquitous)*
The library shall provide convenience initializers: `elf_init_i386()`, `elf_init_x86_64()`, `elf_init_arm()`, `elf_init_aarch64()`.

**REQ-ELF-022** *(Ubiquitous)*
The library shall allow adding sections with `elf_add_section()`, specifying name, type, flags, alignment, entry size, and initial data.

**REQ-ELF-023** *(Ubiquitous)*
The library shall allow adding symbols with `elf_add_symbol()`, specifying name, value, size, type, binding, visibility, and section index.

**REQ-ELF-024** *(Ubiquitous)*
The library shall allow adding relocations with `elf_add_relocation()`, specifying section, offset, symbol, type, and addend.

**REQ-ELF-025** *(Ubiquitous)*
The library shall allow adding program headers / segments with `elf_add_segment()`, specifying type, flags, alignment, and member sections.

**REQ-ELF-026** *(Ubiquitous)*
The library shall automatically generate `.symtab`, `.strtab`, and `.shstrtab` sections during finalization.

**REQ-ELF-027** *(Ubiquitous)*
The library shall set `e_flags` via `elf_set_flags()` and expose it via `elf_flags()`.

### 4.3 ELF Writing

**REQ-ELF-030** *(Ubiquitous)*
The library shall write finalized ELF objects to a file path via `elf_write_file()` or to a memory buffer via `elf_write_buffer()`.

**REQ-ELF-031** *(Ubiquitous)*
The library shall compute section offsets, sizes, and string table indices during a layout pass before writing.

**REQ-ELF-032** *(Ubiquitous)*
The library shall write all multi-byte values in the endianness specified at creation time.

**REQ-ELF-033** *(Ubiquitous)*
The library shall emit REL-format relocations for ELF32 objects (i386, ARM) and RELA-format relocations for ELF64 objects (x86-64, AArch64) by default.

**REQ-ELF-034** *(State-driven)*
While an object is marked read-only or finalized, the library shall reject mutation calls with `ELF_ERR_STATE`.

**REQ-ELF-035** *(Ubiquitous)*
The library shall produce deterministic output: identical API calls in identical order shall produce byte-identical ELF files.

### 4.4 Relocation Engine

**REQ-ELF-040** *(Ubiquitous)*
The library shall provide a pluggable relocation backend interface: `apply_reloc`, `reloc_size`, `is_pc_relative`.

**REQ-ELF-041** *(Ubiquitous)*
The library shall register built-in backends for EM_386 and EM_X86_64 at initialization.

**REQ-ELF-042** *(Ubiquitous)*
The library shall register built-in backends for EM_ARM and EM_AARCH64 at initialization.

**REQ-ELF-043** *(Ubiquitous)*
The library shall allow external registration of custom backends via `elf_register_reloc_backend()`.

**REQ-ELF-044** *(Ubiquitous)*
The library shall apply relocations via `elf_apply_relocation()`, computing the result value using the appropriate backend.

**REQ-ELF-045** *(Event-driven)*
When a relocation result overflows the target field width, the library shall return `ELF_ERR_RELOC` with a diagnostic identifying the relocation type and overflow.

**REQ-ELF-046** *(Event-driven)*
When a relocation type is not recognized by any backend, the library shall return `ELF_ERR_UNSUPPORTED` with the machine and type in the diagnostic.

**REQ-ELF-047** *(Ubiquitous)*
The library shall provide `elf_reloc_size_for_machine()` returning the byte width of a relocation result for a given machine and type.

**REQ-ELF-048** *(Ubiquitous)*
The library shall provide `elf_reloc_is_pc_relative_for_machine()` and `elf_reloc_is_tls_for_machine()` classification functions.

**REQ-ELF-049** *(Ubiquitous)*
The library shall provide `elf_reloc_name_for_machine()` returning a human-readable string (e.g., `"R_X86_64_PC32"`) for any known relocation type.

### 4.5 i386 Relocation Backend

**REQ-ELF-050** *(Ubiquitous)*
The i386 backend shall compute relocations for: `R_386_NONE`, `R_386_32`, `R_386_PC32`, `R_386_GOT32`, `R_386_PLT32`, `R_386_RELATIVE`, `R_386_GOTOFF`, `R_386_GOTPC`, and all `R_386_TLS_*` types.

**REQ-ELF-051** *(Ubiquitous)*
The i386 backend shall compute relocations for: `R_386_COPY`, `R_386_GLOB_DAT`, `R_386_JMP_SLOT`, `R_386_IRELATIVE`, `R_386_GOT32X`, `R_386_SIZE32`.

**REQ-ELF-052** *(Ubiquitous)*
The i386 backend shall compute relocations for 16-bit and 8-bit types: `R_386_16`, `R_386_PC16`, `R_386_8`, `R_386_PC8`.

**REQ-ELF-053** *(Ubiquitous)*
The i386 backend shall check overflow: 32-bit unsigned for `R_386_32`, 32-bit signed for `R_386_PC32`, 16-bit for `R_386_16`/`R_386_PC16`, 8-bit for `R_386_8`/`R_386_PC8`.

### 4.6 x86-64 Relocation Backend

**REQ-ELF-060** *(Ubiquitous)*
The x86-64 backend shall compute relocations for: `R_X86_64_NONE`, `R_X86_64_64`, `R_X86_64_PC32`, `R_X86_64_GOT32`, `R_X86_64_PLT32`, `R_X86_64_GOTPCREL`, `R_X86_64_32`, `R_X86_64_32S`, and all `R_X86_64_TLS*` types.

**REQ-ELF-061** *(Ubiquitous)*
The x86-64 backend shall compute relocations for: `R_X86_64_COPY`, `R_X86_64_GLOB_DAT`, `R_X86_64_JUMP_SLOT`, `R_X86_64_RELATIVE`, `R_X86_64_IRELATIVE`, `R_X86_64_GOTPCRELX`, `R_X86_64_REX_GOTPCRELX`.

**REQ-ELF-062** *(Ubiquitous)*
The x86-64 backend shall compute: `R_X86_64_PC64`, `R_X86_64_GOTOFF64`, `R_X86_64_GOTPC32`, `R_X86_64_SIZE32`, `R_X86_64_SIZE64`.

**REQ-ELF-063** *(Ubiquitous)*
The x86-64 backend shall compute TLSDESC relocations: `R_X86_64_GOTPC32_TLSDESC`, `R_X86_64_TLSDESC_CALL`, `R_X86_64_TLSDESC`.

### 4.7 ARMv7 Relocation Backend

**REQ-ELF-070** *(Ubiquitous)*
The ARM backend shall compute relocations for all static types: `R_ARM_ABS32`, `R_ARM_REL32`, `R_ARM_PC24`, `R_ARM_CALL`, `R_ARM_JUMP24`, `R_ARM_PLT32`, `R_ARM_GOTOFF32`, `R_ARM_GOTPC`, `R_ARM_GOT_BREL`, `R_ARM_PREL31`.

**REQ-ELF-071** *(Ubiquitous)*
The ARM backend shall compute MOVW/MOVT relocations by extracting and reinserting the 16-bit immediate from ARM instruction fields (bits[19:16] + bits[11:0]).

**REQ-ELF-072** *(Ubiquitous)*
The ARM backend shall compute Thumb branch relocations (`R_ARM_THM_CALL`, `R_ARM_THM_JUMP24`) by decoding and re-encoding J1/J2/imm10/imm11 fields.

**REQ-ELF-073** *(Ubiquitous)*
The ARM backend shall compute Thumb MOVW/MOVT relocations by extracting and reinserting the 16-bit immediate from Thumb-2 fields (imm4:i:imm3:imm8).

**REQ-ELF-074** *(Ubiquitous)*
The ARM backend shall apply the Thumb interwork bit (T) when the target symbol is a Thumb function.

**REQ-ELF-075** *(Ubiquitous)*
The ARM backend shall compute group relocations (`R_ARM_ALU_PC_G*`, `R_ARM_LDR_PC_G*`, SB variants) per ARM EABI §4.6.1.4.

**REQ-ELF-076** *(Ubiquitous)*
The ARM backend shall handle `R_ARM_V4BX` by rewriting `BX Rm` to `MOV PC, Rm`.

### 4.8 AArch64 Relocation Backend

**REQ-ELF-080** *(Ubiquitous)*
The AArch64 backend shall compute page-relative relocations: `R_AARCH64_ADR_PREL_PG_HI21` using `Page(S+A) - Page(P)`, and `R_AARCH64_ADD_ABS_LO12_NC` using `(S+A) & 0xFFF`.

**REQ-ELF-081** *(Ubiquitous)*
The AArch64 backend shall compute branch relocations: `R_AARCH64_JUMP26` and `R_AARCH64_CALL26` using `(S+A-P) >> 2` with ±128MB range check.

**REQ-ELF-082** *(Ubiquitous)*
The AArch64 backend shall compute conditional branch and test-branch relocations: `R_AARCH64_CONDBR19` (±1MB), `R_AARCH64_TSTBR14` (±32KB).

**REQ-ELF-083** *(Ubiquitous)*
The AArch64 backend shall compute load/store offset relocations with alignment checking: `R_AARCH64_LDST8/16/32/64/128_ABS_LO12_NC`.

**REQ-ELF-084** *(Ubiquitous)*
The AArch64 backend shall compute MOVW relocations for all groups G0–G3, including signed variants that may flip MOVZ↔MOVN.

**REQ-ELF-085** *(Ubiquitous)*
The AArch64 backend shall provide instruction field helper functions for extracting and inserting imm26, imm19, imm14, ADR imm, imm12, and MOVW imm16.

### 4.9 Validation

**REQ-ELF-090** *(Ubiquitous)*
The library shall validate ELF structure via `elf_validate()`, checking magic bytes, class, data encoding, header sizes, and section/program header consistency.

**REQ-ELF-091** *(Ubiquitous)*
The library shall detect and report overlapping sections, out-of-bounds offsets, and invalid string table references.

**REQ-ELF-092** *(State-driven)*
While validating an EM_386 object, the library shall reject ELFCLASS64 and ELFDATA2MSB.

**REQ-ELF-093** *(State-driven)*
While validating an EM_X86_64 object, the library shall reject ELFCLASS32 and ELFDATA2MSB.

**REQ-ELF-094** *(State-driven)*
While validating an EM_ARM object, the library shall reject ELFCLASS64 and validate `e_flags` EABI version and float ABI consistency.

**REQ-ELF-095** *(State-driven)*
While validating an EM_AARCH64 object, the library shall reject ELFCLASS32.

**REQ-ELF-096** *(Ubiquitous)*
The library shall accumulate multiple validation diagnostics (up to a configurable limit) rather than stopping at the first error.

**REQ-ELF-097** *(Ubiquitous)*
The library shall classify diagnostics by severity level: error, warning, info.

### 4.10 ARM Build Attributes

**REQ-ELF-100** *(Event-driven)*
When an EM_ARM object contains a `SHT_ARM_ATTRIBUTES` section, the library shall parse it into vendor subsections and tag-value pairs per ARM EABI §2.2.3.

**REQ-ELF-101** *(Ubiquitous)*
The library shall decode all standard `aeabi` tags (Tag_CPU_name through Tag_Virtualization_use) and expose them via `elf_arm_attribute_tag_at()` and `elf_arm_attribute_value_at()`.

**REQ-ELF-102** *(Ubiquitous)*
The library shall skip unknown vendor subsections without error.

**REQ-ELF-103** *(Ubiquitous)*
The library shall provide `elf_arm_attribute_count()` for the number of parsed tags.

### 4.11 GNU Properties and Notes

**REQ-ELF-110** *(Ubiquitous)*
The library shall parse `.note.gnu.property` sections and expose individual properties via `elf_gnu_property_at()`.

**REQ-ELF-111** *(Ubiquitous)*
The library shall provide `elf_x86_isa_level()` returning the `GNU_PROPERTY_X86_ISA_1_NEEDED` bitmask (0 if absent).

**REQ-ELF-112** *(Ubiquitous)*
The library shall provide `elf_x86_feature_flags()` returning the `GNU_PROPERTY_X86_FEATURE_1_AND` bitmask.

**REQ-ELF-113** *(Ubiquitous)*
The library shall provide `elf_aarch64_feature_flags()` returning the `GNU_PROPERTY_AARCH64_FEATURE_1_AND` bitmask (BTI, PAC).

**REQ-ELF-114** *(Ubiquitous)*
The library shall provide `elf_build_id()` to extract `.note.gnu.build-id` contents.

**REQ-ELF-115** *(Ubiquitous)*
The library shall provide `elf_add_gnu_property_x86()` and `elf_add_gnu_property_aarch64()` for creating properties during ELF construction.

### 4.12 DWARF Support

**REQ-ELF-120** *(Ubiquitous)*
The library shall parse `.debug_line` sections (DWARF versions 2–5) and expose file/line/column mappings via `elf_dwarf_line_find()`.

**REQ-ELF-121** *(Ubiquitous)*
The library shall parse `.debug_info` and `.debug_abbrev` for DIE traversal.

**REQ-ELF-122** *(Ubiquitous)*
The library shall support per-architecture DWARF register number mappings.

### 4.13 Link Planning

**REQ-ELF-130** *(Ubiquitous)*
The library shall provide a link planning API (`elf_link_plan_t`) for collecting multiple input objects, resolving symbols, and merging sections.

**REQ-ELF-131** *(Event-driven)*
When link inputs have mismatched `e_machine` values, the library shall reject the link with `ELF_ERR_STATE`.

**REQ-ELF-132** *(Event-driven)*
When merging ARM objects with conflicting float ABI flags, the library shall report an error.

**REQ-ELF-133** *(Ubiquitous)*
The library shall provide hooks for section merge policy, archive extraction, GC, incremental linking, and symbol versioning.

### 4.14 Architecture-Specific Sections

**REQ-ELF-140** *(State-driven)*
While processing EM_ARM objects, the library shall handle `SHT_ARM_EXIDX` sections with `SHF_LINK_ORDER` semantics and `PT_ARM_EXIDX` segments.

**REQ-ELF-141** *(State-driven)*
While processing EM_AARCH64 objects, the library shall handle `PT_AARCH64_MEMTAG_MTE` segments.

**REQ-ELF-142** *(Ubiquitous)*
The library shall preserve and expose ARM mapping symbols (`$a`, `$t`, `$d`) and AArch64 mapping symbols (`$x`, `$d`).

---

## 5. Non-Functional Requirements

**REQ-ELF-200** *(Ubiquitous)*
The library shall be written in C99 with no platform-specific dependencies beyond standard libc.

**REQ-ELF-201** *(Ubiquitous)*
The library shall be safe to use in multi-threaded programs: global state (backend registry) shall be protected by atomic operations.

**REQ-ELF-202** *(Ubiquitous)*
The library shall not crash, invoke undefined behavior, or leak memory on any input, including adversarial or malformed ELF files.

**REQ-ELF-203** *(Ubiquitous)*
The library shall perform bounds checking on all offsets and sizes read from ELF headers before accessing file data.

**REQ-ELF-204** *(Ubiquitous)*
The library shall handle objects with up to 65,536 sections (using `SHT_SYMTAB_SHNDX` extended indices) and up to 16 million symbols.

**REQ-ELF-205** *(Ubiquitous)*
The library shall compile cleanly with `-Wall -Wextra -Werror` on GCC and Clang.

**REQ-ELF-206** *(Ubiquitous)*
The library shall have zero ASAN, UBSAN, or Valgrind findings on the test suite.

**REQ-ELF-207** *(Ubiquitous)*
The library shall be buildable as a host tool (`NATIVE_BUILD=1`) and as a Substrate native library.

---

## 6. User Stories

### US-01: Binutil Reading an Object File

> As a **binutil developer** (nm, readelf, objdump, size), I want to open an ELF file and iterate its symbols, sections, and relocations so that I can display meaningful information to the user.

**REQ-US-01-A** *(Ubiquitous)*
The library shall provide `elf_symbol_count()`, `elf_symbol_at()`, `elf_section_count()`, `elf_section_at()`, `elf_section_get()`, `elf_reloc_count()`, and `elf_reloc_at()` accessors for sequential iteration.

**REQ-US-01-B** *(Ubiquitous)*
The library shall provide `elf_symbol_name()`, `elf_symbol_value()`, `elf_symbol_size()`, `elf_symbol_type()`, `elf_symbol_bind()`, `elf_symbol_vis()`, and `elf_symbol_shndx()` for per-symbol queries.

**REQ-US-01-C** *(Ubiquitous)*
The library shall provide `elf_section_name()`, `elf_section_type()`, `elf_section_flags()`, `elf_section_addr()`, `elf_section_size()`, `elf_section_data()`, and `elf_section_entsize()` for per-section queries.

**REQ-US-01-D** *(Ubiquitous)*
The library shall provide `elf_reloc_offset()`, `elf_reloc_type()`, `elf_reloc_addend()`, `elf_reloc_symbol()`, and `elf_reloc_section()` for per-relocation queries.

### US-02: Assembler Producing an Object File

> As an **assembler developer**, I want to create an ELF object from scratch, add sections with machine code, add symbols, and add relocations so that the output is linkable by the Substrate or GNU linker.

**REQ-US-02-A** *(Ubiquitous)*
The library shall allow setting the entry point via `elf_set_entry()`.

**REQ-US-02-B** *(Ubiquitous)*
The library shall allow appending raw data to a section after creation.

**REQ-US-02-C** *(Event-driven)*
When `elf_write_file()` is called, the library shall finalize layout and produce a valid ELF file in a single call.

**REQ-US-02-D** *(Unwanted behavior)*
If `elf_add_relocation()` is called with a symbol from a different object, then the library shall return `ELF_ERR_STATE`.

### US-03: Linker Resolving Relocations

> As a **linker developer**, I want to apply relocations across multiple input objects so that I can produce a final executable or shared library.

**REQ-US-03-A** *(Ubiquitous)*
The library shall provide `elf_apply_relocation()` which computes the relocated value using the registered backend for the object's machine type.

**REQ-US-03-B** *(Ubiquitous)*
The library shall provide relocation hooks (`before_apply`, `after_apply`, `incremental_note`) for linker-specific relaxation and logging.

**REQ-US-03-C** *(Event-driven)*
When a relocation overflows, the library shall return `ELF_ERR_RELOC` with the machine type, relocation type, place address, and symbol value in the diagnostic.

**REQ-US-03-D** *(Ubiquitous)*
The library shall provide `elf_link_plan_add_input()` to register objects for multi-object linking.

### US-04: Strip Tool Removing Debug Info

> As a **strip developer**, I want to selectively copy sections from an input object to an output object, omitting debug and symbol table sections, so that I can produce a smaller binary.

**REQ-US-04-A** *(Ubiquitous)*
The library shall allow creating an output object that copies selected sections from an input object while preserving section indices and inter-section references.

**REQ-US-04-B** *(Event-driven)*
When a section referenced by a program header is not copied, the library shall flag the inconsistency via diagnostic.

**REQ-US-04-C** *(Ubiquitous)*
The library shall preserve program headers and segment structure when copying ET_EXEC/ET_DYN objects.

### US-05: readelf Displaying ARM Build Attributes

> As a **readelf developer**, I want to parse and display ARM build attributes so that users can inspect compiler settings, ABI choices, and ISA features of ARM objects.

**REQ-US-05-A** *(Event-driven)*
When an EM_ARM object contains `.ARM.attributes`, the library shall expose each parsed tag via indexed accessors.

**REQ-US-05-B** *(Ubiquitous)*
The library shall distinguish between integer-valued and string-valued attribute tags.

**REQ-US-05-C** *(Ubiquitous)*
The library shall expose the vendor name (e.g., `"aeabi"`) for each attribute subsection.

### US-06: Linker Merging GNU Properties

> As a **linker developer**, I want to merge `.note.gnu.property` notes across multiple inputs using the correct merge rules (OR for ISA levels, AND for feature flags) so that the output correctly reflects the combined requirements.

**REQ-US-06-A** *(Ubiquitous)*
The library shall provide `elf_x86_isa_level()` and `elf_aarch64_feature_flags()` to read properties from input objects.

**REQ-US-06-B** *(Ubiquitous)*
The library shall provide `elf_add_gnu_property_x86()` and `elf_add_gnu_property_aarch64()` to write merged properties to the output object.

**REQ-US-06-C** *(Event-driven)*
When an input object has no `.note.gnu.property`, the library shall return 0 from the property query functions rather than failing.

### US-07: addr2line Resolving Debug Information

> As an **addr2line developer**, I want to look up source file and line number for a given program address so that users can map crash addresses to source code.

**REQ-US-07-A** *(Ubiquitous)*
The library shall provide `elf_dwarf_line_find(obj, address, &file, &line, &column)` that searches the `.debug_line` program.

**REQ-US-07-B** *(Event-driven)*
When the address is not covered by any compilation unit, the library shall return `ELF_ERR_NOT_FOUND`.

**REQ-US-07-C** *(Ubiquitous)*
The library shall handle DWARF versions 2, 3, 4, and 5 `.debug_line` formats.

### US-08: Fuzz Testing the Library

> As an **elfobj developer**, I want the library to be crash-free on arbitrary input so that binutils using it are safe against malicious ELF files.

**REQ-US-08-A** *(Ubiquitous)*
The library shall validate all header fields before using them as offsets or sizes.

**REQ-US-08-B** *(Ubiquitous)*
The library shall use checked arithmetic for offset + size calculations to prevent integer overflow.

**REQ-US-08-C** *(Ubiquitous)*
The library shall return error codes (never abort/assert) for all recoverable error conditions.

**REQ-US-08-D** *(Ubiquitous)*
The library shall provide a fuzz harness entry point that accepts a raw byte buffer and exercises the full read path.

---

## 7. Error Model

| Error Code          | Meaning                                                   |
|---------------------|-----------------------------------------------------------|
| `ELF_OK`            | Success                                                   |
| `ELF_ERR_IO`        | File I/O failure                                          |
| `ELF_ERR_FORMAT`    | Invalid ELF structure (bad magic, truncated, corrupt)     |
| `ELF_ERR_OOM`       | Memory allocation failure                                 |
| `ELF_ERR_STATE`     | API misuse (mutation of finalized object, cross-object ref) |
| `ELF_ERR_UNSUPPORTED` | Unsupported machine type, relocation type, or feature   |
| `ELF_ERR_RELOC`     | Relocation overflow or application error                  |
| `ELF_ERR_NOT_FOUND` | Requested item (symbol, address, property) not found      |

**REQ-ELF-300** *(Ubiquitous)*
Every public API function shall return `elf_err_t` or a pointer (NULL on error), and set a diagnostic string retrievable via `elf_last_error()`.

---

## 8. Traceability Matrix

| Requirement       | User Story | Tasklist Section |
|-------------------|------------|------------------|
| REQ-ELF-010–019   | US-01      | §5 (Read)        |
| REQ-ELF-020–027   | US-02      | §7 (Create)      |
| REQ-ELF-030–035   | US-02, US-04 | §6 (Write)     |
| REQ-ELF-040–049   | US-03      | §2, §3, §12      |
| REQ-ELF-050–053   | US-03      | §12a (i386)      |
| REQ-ELF-060–063   | US-03      | §12b (x86-64)    |
| REQ-ELF-070–076   | US-03      | §2 (ARM)         |
| REQ-ELF-080–085   | US-03      | §3 (AArch64)     |
| REQ-ELF-090–097   | US-01, US-08 | §4, §13 (Validate) |
| REQ-ELF-100–103   | US-05      | §10 (Build Attr)  |
| REQ-ELF-110–115   | US-06      | §14 (GNU Prop)    |
| REQ-ELF-120–122   | US-07      | §8 (DWARF)        |
| REQ-ELF-130–133   | US-03      | §9 (Link)         |
| REQ-ELF-140–142   | US-01, US-05 | §1f–1h, §5      |
| REQ-ELF-200–207   |            | (Non-functional)   |
| REQ-ELF-300       | US-08      | (Error model)      |

---

## 9. Acceptance Criteria

1. `libelfobj.a` builds cleanly on host Linux and Substrate target.
2. All four architecture backends pass encoding unit tests for every relocation type.
3. Read→write round-trip produces byte-identical output for i386, x86-64, ARM, and AArch64 test objects.
4. `readelf -a` structural validation passes on all library-generated objects.
5. Objects produced by Substrate `as` via `libelfobj` are linkable by GNU `ld` and Substrate `ld`.
6. ARM build attributes from GCC-produced objects are parsed and all standard tags decoded.
7. GNU property ISA level and feature flags are correctly read, merged, and written.
8. Validation rejects class/endian mismatches per architecture.
9. 24-hour fuzz run produces zero crashes (0 ASAN/UBSAN findings).
10. `nm`, `readelf`, `strip`, `size`, `objdump`, `objcopy`, `ar`, `addr2line` all use `libelfobj` exclusively (no BFD, no libelf).
