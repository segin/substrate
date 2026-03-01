# as Rollout and Release Gating

## Purpose
This document defines rollout controls for the Substrate `as` wrapper, including migration steps, ABI gates, and post-release regression handling.

## Migration Checklist
- Keep backend-delegated assembly path (`gcc -c -x assembler-with-cpp`) as default until native encoder parity gates are met.
- Preserve CLI compatibility (`-32/-64/-I/-D/-Wa/-march/-mtune/-g`) for `cc` and existing build scripts.
- Maintain ET_REL class/machine validation via `libelfobj` after backend output.
- Keep differential checks against direct GNU backend on representative corpora.
- Gate all behavior changes with test updates in `tests/usr.bin/as/`.

## Release Criteria and Gating Matrix
- Build integration: `make -C usr.bin/as` and `make -C usr.bin/as NATIVE_BUILD=1` pass.
- Driver integration: `cc` `-S` and `-c` pipelines succeed when `AS` points to `usr.bin/as/as`.
- ABI metadata: produced objects are ET_REL with expected ELF class and machine for `-32/-64`.
- Toolchain compatibility: objects round-trip through `objdump`/`readelf` and `ld` (`-r` and freestanding link smoke).
- Determinism: repeated builds of identical inputs/options produce byte-identical outputs.
- Diagnostics/safety: invalid inputs fail deterministically with stable wrapper diagnostics; configured limits enforce bounded behavior.
- Fuzz/perf baseline: fuzz-smoke harness and large-file throughput script complete without crashes.

## Post-Release Regression Triage Process
- Intake: file regression with input source/options, expected/actual object metadata, and command transcript.
- Classification:
  - `ABI-break`: ELF type/class/machine/reloc incompatibility.
  - `Compat-break`: behavior diverges from GNU backend contract.
  - `Determinism-break`: output unstable across repeated runs.
  - `Diag-break`: error quality or stability regression.
  - `Perf-break`: throughput or resource regression.
- Reproduction: add failing case to `tests/usr.bin/as/` before fix.
- Resolution: patch wrapper/docs/tests together; require green assembler suite before merge.
- Backport policy: prioritize `ABI-break` and `Compat-break` for immediate stable backport.
