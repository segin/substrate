# `usr.bin/ld` Commit Template (Requirement/Story Tags)

Use this format for linker commits so requirement and story traceability is explicit.

## Subject Format

`ld: <concise action>`

Example:

`ld: enforce strict -m parser with conflict diagnostics`

## Body Format

```
Reqs: LD-U-010, LD-E-007
Stories: US-301
Tests: tests/usr.bin/ld/test_mode_parser.sh

Summary:
- <key behavior change 1>
- <key behavior change 2>

Compatibility:
- <gnu/lld behavior notes>
```

## Rules

1. `Reqs:` is required for every linker change.
2. `Stories:` is required when a story ID exists in `TASKLIST_LINKER.md`; otherwise use `Stories: n/a`.
3. `Tests:` must list exact scripts/files run for verification.
4. Keep the subject line behavior-oriented and avoid generic text like "fix stuff".
