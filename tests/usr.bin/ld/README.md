# `tests/usr.bin/ld` Test Taxonomy

This directory contains linker-facing regression tests for `usr.bin/ld`.

## Scope

Tests here validate:

- CLI option semantics and diagnostics.
- target-mode selection and compatibility policy.
- ELF output class/type invariants for basic link flows.

## Taxonomy

Use one of these prefixes in each test script purpose:

- `driver`: option parsing, mode selection, warning/error policy.
- `compat`: GNU/lld compatibility-mode behavior.
- `link`: end-to-end link flows (`-r`, ET_EXEC, ET_DYN) and output invariants.
- `diag`: diagnostic quality and failure-path behavior.

## Naming Convention

Script names must follow:

`test_<area>_<behavior>.sh`

Examples:

- `test_link_32_64.sh`
- `test_mode_parser.sh`
- `test_unsupported_option_policy.sh`

## Script Contract

All scripts must:

1. Start with `#!/bin/sh` and `set -eu`.
2. Resolve repo root relative to script path.
3. Use a private temp directory under `${TMPDIR:-/tmp}`.
4. Install cleanup trap for temp artifacts.
5. Print `ok: ...` on success.
6. Exit non-zero with `FAIL: ...` diagnostics on failure.

## Requirement Traceability

Every test script should include a short comment header listing requirement IDs from `usr.bin/ld/SPEC.md` (for example `LD-U-002`, `LD-U-010`, `LD-W-003`) and optional user story IDs.

This supports the pass/fail dashboard and review traceability requirements.
