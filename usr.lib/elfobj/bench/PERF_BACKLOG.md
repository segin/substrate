# libelfobj Performance Backlog

## Hot-path profiling backlog
- Profile `parse_sections()` and `parse_program_headers()` on large ET_DYN images.
- Profile symbol and relocation materialization (`parse_symbols()`, `parse_relocations()`) with >100k symbols.
- Profile relocation backend dispatch (`find_backend()` and `apply_relocation` path) under high relocation counts.
- Profile writer string-table construction and section emission in `elf_write.c`.

## Optimization backlog
- Replace repeated linear section lookups with cached name->index tables in parse-heavy flows.
- Batch allocation for symbol and relocation objects to reduce allocator overhead.
- Add optional fast hash index for `elf_find_symbol()` and relocation target lookup.
- Add architecture-specific relocation hot loops with branch reduction.

## Baseline policy
- `write_10k_symbols_ms` and `link_large_archive_ms` are tracked by `tests/test_perf_gate.sh`.
- The gate is opt-in by default (`ELFOBJ_RUN_PERF_GATE=1`) to avoid unstable CI hosts.
- Thresholds are configurable:
  - `ELFOBJ_MAX_WRITE_10K_MS`
  - `ELFOBJ_MAX_LINK_LARGE_MS`
  - `ELFOBJ_MAX_READ_KERNEL_MS`
