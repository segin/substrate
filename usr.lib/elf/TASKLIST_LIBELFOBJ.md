# `usr.lib/elf` (`libelfobj`) Comprehensive Tasklist

Purpose: long-term actionable checklist for a production-quality ELF object handling library used by assembler, linker, loader tools, and analyzers.

Scope:
- Reader, writer, object model, linker-facing APIs, validation, ABI stability, fuzzing.
- ELF32/ELF64, little/big-endian, SysV ABI + platform ABI behavior.

Execution policy:
- Complete one checkbox at a time.
- Add tests for each feature before marking done.
- Validate both i386 and x86_64 flows.
- Keep memory-safety and deterministic behavior as hard requirements.

---

## 1) Public API and ABI Governance
- [ ] Finalize opaque-handle public API shape in `include/elfobj.h`.
- [ ] Versioned ABI policy and symbol version script.
- [ ] Stable error code taxonomy and structured diagnostics.
- [ ] API lifecycle rules (create/open/close/finalize).
- [ ] Thread-safety contract documentation and enforcement.
- [ ] Reentrancy guarantees for independent objects.
- [ ] Backward-compatible extension pattern for future APIs.
- [ ] `pkg-config` metadata correctness (`elfobj.pc`).
- [ ] Man pages for all public APIs.
- [ ] ABI compliance tests across releases.

## 2) ELF Reader Core
- [ ] Parse ELF header safely with full bounds checks.
- [ ] Detect class (`ELFCLASS32/64`) and endianness.
- [ ] Parse program headers with truncation detection.
- [ ] Parse section headers with truncation/overlap detection.
- [ ] Lazy section payload loading.
- [ ] Memory-backed and file-backed reader entrypoints.
- [ ] Parse notes, dynamic section, and versioning sections.
- [ ] Parse symbol tables (`.symtab`/`.dynsym`).
- [ ] Parse relocation sections (`REL`/`RELA`).
- [ ] Parse and retain unknown sections/extensions safely.

## 3) ELF Writer Core
- [ ] Create new ELF objects for ET_REL/ET_EXEC/ET_DYN.
- [ ] Deterministic section and segment layout planning.
- [ ] Controlled alignment and padding behavior.
- [ ] String table construction (`.strtab`, `.shstrtab`, `.dynstr`).
- [ ] Symbol table construction and index stability.
- [ ] Relocation section emission (`.rel*`/`.rela*`).
- [ ] Program header emission for loadable outputs.
- [ ] Final serialization with overflow checks.
- [ ] Round-trip preservation for untouched sections.
- [ ] Deterministic byte-for-byte output under identical inputs.

## 4) Section and Segment APIs
- [ ] Add/find/remove/reorder sections by API.
- [ ] Section type/flag mutation APIs with validation.
- [ ] Section group/COMDAT support.
- [ ] Mergeable section semantics (`SHF_MERGE`/`SHF_STRINGS`).
- [ ] TLS section handling (`.tdata`/`.tbss`).
- [ ] Note section helpers.
- [ ] Program segment creation and assignment APIs.
- [ ] PT_LOAD/PT_DYNAMIC/PT_INTERP/PT_TLS helpers.
- [ ] Segment alignment and overlap validation.
- [ ] Section-to-segment mapping introspection APIs.

## 5) Symbol and Hash Handling
- [ ] Symbol add/find by name/index.
- [ ] Binding/type/visibility mutation APIs.
- [ ] Duplicate and conflict detection helpers.
- [ ] Symbol version metadata read/write support.
- [ ] Local/global symbol partitioning rules.
- [ ] Undefined/absolute/common symbol handling.
- [ ] SYSV hash generation/lookup.
- [ ] GNU hash generation/lookup.
- [ ] Stable symbol ordering for deterministic builds.
- [ ] Symbol-table validation checks.

## 6) Relocation Framework and Backends
- [ ] Generic relocation object model for REL and RELA.
- [ ] Architecture backend interface (`apply`, `size`, `pc-relative`).
- [ ] i386 relocation backend.
- [ ] x86_64 relocation backend.
- [ ] TLS relocation handling in backend model.
- [ ] Addend and sign-extension correctness.
- [ ] Relocation overflow detection.
- [ ] Unsupported relocation diagnostics with context.
- [ ] Relaxation/incremental-link hooks.
- [ ] Relocation fuzz tests with malformed inputs.

## 7) Linker-Facing Services
- [ ] Multi-object load/merge API for linkers.
- [ ] Symbol resolution helper API across objects.
- [ ] Section merge policy hooks.
- [ ] Archive extraction helper hooks.
- [ ] Dead-section GC integration hooks.
- [ ] Incremental linking metadata hooks.
- [ ] GOT/PLT synthesis helper APIs.
- [ ] Dynamic section construction helpers.
- [ ] Version script integration hooks.
- [ ] Link map/introspection helper APIs.

## 8) Debug and Unwind Sections
- [ ] Preserve `.debug_*` sections in round-trip mode.
- [ ] Read/write `.eh_frame` structures at container level.
- [ ] Symbol/debug cross-reference consistency checks.
- [ ] CFI-related section retention helpers.
- [ ] Optional compressed debug section support hooks.
- [ ] Minimal DWARF section structural validator.
- [ ] Split DWARF section passthrough behavior.
- [ ] Debug section relocation support.
- [ ] Deterministic ordering of debug sections.
- [ ] Debug-info compatibility tests with system tools.

## 9) Validation and Hardening
- [ ] Central validation API with structured diagnostics.
- [ ] Invalid offset/size and truncation detection.
- [ ] Overlap and out-of-range region detection.
- [ ] Section-header/program-header coherence checks.
- [ ] Relocation target/index validity checks.
- [ ] Symbol table/string table coherence checks.
- [ ] Flag/type consistency checks by section kind.
- [ ] Defensive allocation and integer-overflow guards.
- [ ] Configurable strict/permissive validation modes.
- [ ] Security regression suite and crash-free guarantees.

## 10) Performance and Memory Model
- [ ] Zero-copy reads where safe and practical.
- [ ] Optional `mmap` I/O path.
- [ ] Lazy parse/decode of heavy sections.
- [ ] Memory ownership/lifetime model audit.
- [ ] Large-file scalability benchmarks.
- [ ] 10k-symbol object write benchmark target.
- [ ] Kernel image read benchmark target.
- [ ] Link-large-archive benchmark target.
- [ ] Hot-path profiling and optimization backlog.
- [ ] Performance regression gate in CI.

## 11) Cross-Target and Format Coverage
- [ ] ELF32 little-endian coverage.
- [ ] ELF32 big-endian coverage.
- [ ] ELF64 little-endian coverage.
- [ ] ELF64 big-endian coverage.
- [ ] ET_REL full R/W support.
- [ ] ET_EXEC full R/W support.
- [ ] ET_DYN full R/W support.
- [ ] ET_CORE read-only support.
- [ ] ABI supplement conformance checks (i386/x86_64).
- [ ] Compatibility matrix documentation per target/mode.

## 12) Tooling, Docs, and Integration
- [ ] Build targets and install path verification.
- [ ] Example programs (`create`, `reloc`, `merge`, `inspect`) maintained.
- [ ] Fuzz harnesses maintained and continuously run.
- [ ] Bench harnesses maintained.
- [ ] `README.md` and architecture docs synchronized.
- [ ] `ARCHITECTURE.md` section for `usr.lib/elf` kept current.
- [ ] Integration tests with `usr.bin/as` and `usr.bin/ld`.
- [ ] Integration tests with `readelf`, `objdump`, `nm`, `strip`.
- [ ] Migration checklist from ad-hoc ELF code paths.
- [ ] Release checklist and support policy documented.

