# `usr.lib/elfobj` (`libelfobj`) Comprehensive Tasklist

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
- [x] Finalize opaque-handle public API shape in `include/elfobj.h`.
- [x] Versioned ABI policy and symbol version script.
- [x] Stable error code taxonomy and structured diagnostics.
- [x] API lifecycle rules (create/open/close/finalize).
- [x] Thread-safety contract documentation and enforcement.
- [x] Reentrancy guarantees for independent objects.
- [x] Backward-compatible extension pattern for future APIs.
- [x] `pkg-config` metadata correctness (`elfobj.pc`).
- [x] Man pages for all public APIs.
- [x] ABI compliance tests across releases.

## 2) ELF Reader Core
- [x] Parse ELF header safely with full bounds checks.
- [x] Detect class (`ELFCLASS32/64`) and endianness.
- [x] Parse program headers with truncation detection.
- [x] Parse section headers with truncation/overlap detection.
- [x] Lazy section payload loading.
- [x] Memory-backed and file-backed reader entrypoints.
- [x] Parse notes, dynamic section, and versioning sections.
- [x] Parse symbol tables (`.symtab`/`.dynsym`).
- [x] Parse relocation sections (`REL`/`RELA`).
- [x] Parse and retain unknown sections/extensions safely.

## 3) ELF Writer Core
- [x] Create new ELF objects for ET_REL/ET_EXEC/ET_DYN.
- [x] Deterministic section and segment layout planning.
- [x] Controlled alignment and padding behavior.
- [x] String table construction (`.strtab`, `.shstrtab`, `.dynstr`).
- [x] Symbol table construction and index stability.
- [x] Relocation section emission (`.rel*`/`.rela*`).
- [x] Program header emission for loadable outputs.
- [x] Final serialization with overflow checks.
- [x] Round-trip preservation for untouched sections.
- [x] Deterministic byte-for-byte output under identical inputs.

## 4) Section and Segment APIs
- [x] Add/find/remove/reorder sections by API.
- [x] Section type/flag mutation APIs with validation.
- [x] Section group/COMDAT support.
- [x] Mergeable section semantics (`SHF_MERGE`/`SHF_STRINGS`).
- [x] TLS section handling (`.tdata`/`.tbss`).
- [x] Note section helpers.
- [x] Program segment creation and assignment APIs.
- [x] PT_LOAD/PT_DYNAMIC/PT_INTERP/PT_TLS helpers.
- [x] Segment alignment and overlap validation.
- [x] Section-to-segment mapping introspection APIs.

## 5) Symbol and Hash Handling
- [x] Symbol add/find by name/index.
- [x] Binding/type/visibility mutation APIs.
- [x] Duplicate and conflict detection helpers.
- [x] Symbol version metadata read/write support.
- [x] Local/global symbol partitioning rules.
- [x] Undefined/absolute/common symbol handling.
- [x] SYSV hash generation/lookup.
- [x] GNU hash generation/lookup.
- [x] Stable symbol ordering for deterministic builds.
- [x] Symbol-table validation checks.

## 6) Relocation Framework and Backends
- [x] Generic relocation object model for REL and RELA.
- [x] Architecture backend interface (`apply`, `size`, `pc-relative`).
- [x] i386 relocation backend.
- [x] x86_64 relocation backend.
- [x] TLS relocation handling in backend model.
- [x] Addend and sign-extension correctness.
- [x] Relocation overflow detection.
- [x] Unsupported relocation diagnostics with context.
- [x] Relaxation/incremental-link hooks.
- [x] Relocation fuzz tests with malformed inputs.

## 7) Linker-Facing Services
- [x] Multi-object load/merge API for linkers.
- [x] Symbol resolution helper API across objects.
- [x] Section merge policy hooks.
- [x] Archive extraction helper hooks.
- [x] Dead-section GC integration hooks.
- [x] Incremental linking metadata hooks.
- [x] GOT/PLT synthesis helper APIs.
- [x] Dynamic section construction helpers.
- [x] Version script integration hooks.
- [x] Link map/introspection helper APIs.

## 8) Debug and Unwind Sections
- [x] Preserve `.debug_*` sections in round-trip mode.
- [x] Read/write `.eh_frame` structures at container level.
- [x] Symbol/debug cross-reference consistency checks.
- [x] CFI-related section retention helpers.
- [x] Optional compressed debug section support hooks.
- [x] Minimal DWARF section structural validator.
- [x] Split DWARF section passthrough behavior.
- [x] Debug section relocation support.
- [x] Deterministic ordering of debug sections.
- [x] Debug-info compatibility tests with system tools.

## 9) Validation and Hardening
- [x] Central validation API with structured diagnostics.
- [x] Invalid offset/size and truncation detection.
- [x] Overlap and out-of-range region detection.
- [x] Section-header/program-header coherence checks.
- [x] Relocation target/index validity checks.
- [x] Symbol table/string table coherence checks.
- [x] Flag/type consistency checks by section kind.
- [x] Defensive allocation and integer-overflow guards.
- [x] Configurable strict/permissive validation modes.
- [x] Security regression suite and crash-free guarantees.

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
- [ ] `ARCHITECTURE.md` section for `usr.lib/elfobj` kept current.
- [ ] Integration tests with `usr.bin/as` and `usr.bin/ld`.
- [ ] Integration tests with `readelf`, `objdump`, `nm`, `strip`.
- [ ] Migration checklist from ad-hoc ELF code paths.
- [ ] Release checklist and support policy documented.
