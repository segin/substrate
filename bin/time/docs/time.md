# Substrate `time` Implementation Notes

This document reflects the resolution of behaviors and extensions implemented in Substrate's `time` profiling utility.

## Substrate-Specific Features (Fresh Ideas)
- **`--json`**: Generates a standard JSON object containing the `real`, `user`, `sys` properties, as well as the rusage statistics limits like `page_faults` and `exit_status`. Ideal for deterministic programmatic benchmarking.
- **Clock Selection**: We offer `--monotonic` and `--realtime`. `monotonic` is highly recommended (and chosen natively) to avoid negative duration drift against NTP resets mid-run.

## Compliance Precedences
- **BSD Options Win**: Where GNU and BSD diverge in single-character switch significance:
  - `-h`: Human-readable times (BSD), instead of `--help` (GNU).
  - `-f`: Supports formatting injection similar to BSD's NetBSD csh-style variables. GNU formatting injection is accessed exclusively via `--format=`.
- **Totals format default**: Our fallback matches the single-line structure historically present in FreeBSD.
- POSIX structure enforces: `sys`, `user`, `real` standard layout when `-p` is raised, output securely to `stderr`.

## Error States
- Error passthroughs execute identically to BSD semantics: Exec errors return `126`, pathing resolution misses yield `127`, and successful executions perfectly mimic the child `WEXITSTATUS`.
