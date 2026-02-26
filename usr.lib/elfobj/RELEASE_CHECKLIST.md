# libelfobj Release Checklist

## Build and Packaging
- `make -C usr.lib/elfobj clean all`
- `make -C usr.lib/elfobj/tests`
- `make -C usr.lib/elfobj/fuzz`
- `make -C usr.lib/elfobj/bench`
- Verify `elfobj.pc` install metadata and include/lib destinations.

## Validation and Compatibility
- Run `usr.lib/elfobj/tests/test_abi_surface.sh`.
- Run full tests including `test_matrix`, `test_debug_unwind`, `test_validate_hardening`.
- Run `usr.lib/elfobj/tests/test_tool_integration.sh`.
- Run `usr.lib/elfobj/tests/test_build_install.sh`.

## Security and Quality
- Run fuzz smoke harnesses (`make -C usr.lib/elfobj/fuzz all` and execute binaries).
- Review diagnostic output format stability for `elf_validate_ex()` and debug validators.
- Confirm no new crashes on malformed corpus samples.

## Performance
- Run `usr.lib/elfobj/bench/bench_elf` and capture metrics.
- Run `usr.lib/elfobj/tests/test_perf_gate.sh` with project thresholds.
- Update `usr.lib/elfobj/bench/PERF_BACKLOG.md` if regressions or new hotspots are found.

## Documentation and Support Policy
- Sync `README.md`, `COMPATIBILITY_MATRIX.md`, and `ARCHITECTURE.md` entries.
- Ensure man pages reflect public API changes.
- Confirm ABI policy compatibility (`ABI_POLICY.md`, `libelfobj.map`, API baseline file).

## Support Policy
- Public API in `include/elfobj.h` follows additive-compatibility rules.
- Breaking API/ABI changes require explicit versioning and migration notes.
- Security fixes and parser hardening are prioritized over non-critical feature work.
