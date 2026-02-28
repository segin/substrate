# `usr.bin/elfedit` Tasklist

Goal: implement `elfedit` mutations on top of `libelfobj`.

---

## 1. `libelfobj` Integration

- [x] Open input with `elf_open()`.
- [x] Apply mutations via `libelfobj` setter APIs.
- [x] Validate with `elf_validate()` before writing.
- [x] Write with `elf_write_file()`.
- [x] `elf_close()` on all handles.
- [x] In‑place mode: open, mutate, write to temp, rename over original.

## 2. ELF Header Edits

### 2a. `--output-type=<type>`
- [x] Set `e_type` via `elf_set_type()`.
- [x] Accept: `none` (0), `rel` (1), `exec` (2), `dyn` (3), `core` (4), or numeric value.
- [x] Warn if changing type would make file structurally inconsistent (e.g. ET_EXEC → ET_REL without removing program headers).

### 2b. `--output-machine=<machine>`
- [x] Set `e_machine` via `elf_set_machine()`.
- [x] Accept: `i386` (3), `x86_64`/`x86-64` (62), `arm` (40), `aarch64` (183), `mips` (8), `riscv` (243), or numeric value.
- [x] Warn that changing machine doesn't re‑encode instructions or relocations.

### 2c. `--output-osabi=<osabi>`
- [x] Set `EI_OSABI` byte in `e_ident`.
- [x] Accept: `none`/`sysv` (0), `linux` (3), `freebsd` (9), `substrate` (if assigned), or numeric.

### 2d. `--output-abiversion=<version>`
- [x] Set `EI_ABIVERSION` byte.
- [x] Accept numeric value 0–255.

### 2e. `--output-flags=<flags>`
- [x] Set `e_flags` field.
- [x] Accept hex or decimal value.

### 2f. `--output-entry=<addr>`
- [x] Set `e_entry` via `elf_set_entry()`.
- [x] Accept hex (`0x...`) or decimal address.

## 3. Section Header Edits

- [x] `--set-section-type <name>=<type>`: change `sh_type` via `elf_section_set_type()`.
  - [x] Accept: `progbits`, `nobits`, `note`, `symtab`, `strtab`, `rela`, `rel`, `dynamic`, `hash`, or numeric.
- [x] `--set-section-flags <name>=<flags>`: change `sh_flags` via `elf_section_set_flags()`.
  - [x] Accept: comma‑separated list of `alloc`, `write`, `execinstr`, `merge`, `strings`, `tls`, `group`, `compressed`, or hex value.
- [x] `--set-section-align <name>=<align>`: change `sh_addralign` via `elf_section_set_align()`.
- [x] `--rename-section <old>=<new>`: change section name in `.shstrtab`.

## 4. Program Header Edits

- [x] `--set-segment-type <idx>=<type>`: change `p_type` (accept named types or numeric).
- [x] `--set-segment-flags <idx>=<flags>`: change `p_flags` (accept `r`, `w`, `x` combo or hex).
- [x] `--set-segment-align <idx>=<align>`: change `p_align`.
- [x] Note: segment content is not modified — these are metadata‑only edits.

## 5. Validation and Safety

### 5a. Pre‑Write Validation
- [x] Call `elf_validate()` with `ELF_VALIDATE_STRICT` before writing.
- [x] Report all diagnostics from `elf_last_diagnostics()`.
- [x] Refuse to write if strict validation fails (unless `--force`).

### 5b. Dry‑Run Mode (`--dry-run` / `-n`)
- [x] Apply all mutations, run validation, report results — do not write.
- [x] Print summary of changes that would be made.
- [x] Exit 0 if valid, 1 if validation would fail.

### 5c. Unsafe Operations
- [x] `--force` / `-f`: write even if validation fails.
- [x] Print `WARNING: writing structurally invalid ELF` to stderr.
- [x] Never silently produce invalid output (always diagnostic).

### 5d. Guardrails
- [x] Warn on type changes that break structural invariants (e.g. ET_EXEC → ET_REL).
- [x] Warn on machine changes (instructions not re‑encoded).
- [x] Warn on flags changes that conflict with machine ABI.
- [x] Refuse to edit core files by default (allow with `--force`).

## 6. Additional Options

- [x] `--input-mmap`: hint to use memory‑mapped I/O for large files.
- [x] `-v` / `--verbose`: print each field change as it's applied.
- [x] `-o <file>` / `--output=<file>`: write to different file (don't modify in‑place).
- [x] `--version` / `-V`: print version.
- [x] `--help` / `-h`: print usage.

## 7. Error Handling

- [x] Non‑ELF input: `elfedit: <file>: not an ELF file`, exit 1.
- [x] Unknown type/machine/osabi name: `elfedit: unknown <thing>: <value>`, exit 1.
- [x] Section name not found: `elfedit: section '<name>' not found`, exit 1.
- [x] Segment index out of range: `elfedit: segment index <N> out of range (0–<max>)`, exit 1.
- [x] No edits requested: warn, exit 0 (no‑op).
- [x] Write failure: remove temp file, report error, exit 1.

## 8. Build System

- [x] Create `Makefile` linking `libelfobj.a`.
- [x] `NATIVE_BUILD=1` support.
- [x] `install` to `$(DESTDIR)/usr/bin/elfedit`.

## 9. Testing

### 9a. ELF Header Tests
- [x] Change `e_type`: ET_REL → ET_DYN → readelf confirms.
- [x] Change `e_machine`: EM_386 → EM_X86_64 → readelf confirms.
- [x] Change `EI_OSABI`: ELFOSABI_NONE → ELFOSABI_LINUX → readelf confirms.
- [x] Change `e_entry`: set to 0xDEAD → readelf confirms.
- [x] Change `e_flags`: set arbitrary value → readelf confirms.

### 9b. Section Header Tests
- [x] Change `.data` flags to add `SHF_EXECINSTR` → readelf confirms.
- [x] Change `.comment` type to `SHT_NOTE` → readelf confirms.
- [x] Change `.text` alignment → readelf confirms.
- [x] Rename `.text` to `.code` → readelf confirms.

### 9c. Program Header Tests
- [x] Change `PT_LOAD` flags from `PF_R|PF_X` to `PF_R|PF_W|PF_X` → readelf confirms.
- [x] Change segment alignment → readelf confirms.

### 9d. Validation Tests
- [x] Legal edit: exits 0, output valid.
- [x] Illegal edit without `--force`: exits 1, original unchanged.
- [x] Illegal edit with `--force`: exits 0, output written with warning.

### 9e. Dry‑Run Tests
- [x] `--dry-run`: no file modification, diagnostic output.
- [x] `--dry-run` + illegal edit: reports validation failure.

### 9f. Safety Tests
- [x] In‑place edit preserves file permissions.
- [x] Write failure: original untouched, temp cleaned up.
- [x] Edit core file without `--force`: rejected.

### 9g. Edge Cases
- [x] ELF32 and ELF64 inputs.
- [x] ELF with no sections (only program headers).
- [x] Multiple edits in one invocation (e.g. change type + machine + osabi).
- [x] No‑op invocation (no edit flags): exits 0 quietly.

### 9h. Round‑Trip Tests
- [x] Change type → change back → file functionally identical.
- [x] Compare with host `elfedit` output on same operation.

## 10. Man Page

- [x] Write `elfedit.1` covering all edit operations, validation, and safety flags.
- [x] Document named values for type/machine/osabi in tables.
- [x] Install to `$(DESTDIR)/usr/share/man/man1/`.
