# libelfobj

`libelfobj` is a production-oriented ELF object handling library for Substrate tools.

## Scope
- Parse ELF32/ELF64 in little-endian and big-endian modes.
- Support ET_REL, ET_EXEC, ET_DYN, and read-only ET_CORE parsing.
- Build/modify sections, symbols, and relocations.
- Apply relocations through architecture backends (i386/x86_64) with overflow and TLS checks.
- Emit valid ELF objects with generated string/symbol/relocation tables.
- Provide linker-facing object merge API and validation diagnostics.
- Provide linker-plan APIs with merge/archive/GC/version/incremental hooks and link-map introspection.
- Parse and validate program headers, notes, dynamic, and versioning sections.
- Provide section mutation/reorder APIs and explicit segment mapping helpers.
- Provide symbol versioning metadata, deterministic symbol ordering, and hash lookups.

## Build
- `make -C usr.lib/elfobj`
- `make -C usr.lib/elfobj test NATIVE_BUILD=1`

## Install
- `make -C usr.lib/elfobj install DESTDIR=...`

## API
Public API is defined in `include/elfobj.h`.

Lifecycle:
- `elf_open` / `elf_open_memory` create read-only objects.
- `elf_create` creates mutable objects.
- `elf_finalize` freezes object state.
- `elf_write_file` finalizes mutable objects automatically.
- `elf_close` releases all resources.

Thread-safety and reentrancy:
- Independent `elfobj_t` instances are reentrant and safe for concurrent use.
- A single `elfobj_t` requires external synchronization for concurrent mutation.
- Relocation backend registration is serialized internally.

## Memory and I/O Model
- `elf_open_memory()` copies input bytes into internal storage.
- `elf_open_memory_nocopy()` uses caller-provided bytes as a zero-copy read-only view.
- `elf_open_with_options()` and `elf_open_memory_with_options()` accept:
  - `ELFOBJ_OPEN_NOCOPY`
  - `ELFOBJ_OPEN_USE_MMAP`
  - `ELFOBJ_OPEN_LAZY_PARSE`
- `elf_open()` can still use environment toggles (`ELFOBJ_USE_MMAP`, `ELFOBJ_LAZY_PARSE`).
- Parser section payloads are referenced directly from backing image buffers (no section payload copy on read path).
- Optional lazy symbol/relocation parse can be enabled with `ELFOBJ_LAZY_PARSE=1`; data is materialized on first symbol/relocation query.

## Performance
- Benchmark harness: `make -C usr.lib/elfobj/bench`.
- Included benchmarks:
  - 10k-symbol object write (`write_10k_symbols_ms`)
  - Large archive/link simulation (`link_large_archive_ms`)
  - Kernel image read benchmark (`read_kernel_image_ms`, when `ELFOBJ_BENCH_IMAGE` is provided)
- Optional regression gate: `ELFOBJ_RUN_PERF_GATE=1 make -C usr.lib/elfobj/tests`.
- Profiling backlog and optimization priorities: `usr.lib/elfobj/bench/PERF_BACKLOG.md`.

## Stability
`libelfobj` uses opaque handles and an error-code based ABI intended for long-term stability.
See `usr.lib/elfobj/ABI_POLICY.md` and `usr.lib/elfobj/libelfobj.map`.
