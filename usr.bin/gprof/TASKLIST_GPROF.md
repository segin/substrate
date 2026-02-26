# `usr.bin/gprof` Tasklist

Goal: implement `gprof`-compatible analysis for `gmon` data + ELF symbols.

- [ ] Parse profiling data files and correlate with symbols via `libelfobj`.
- [ ] Implement flat profile and call-graph reports.
- [ ] Handle missing/partial symbols robustly.
- [ ] Support archive/shared-object symbol resolution as needed.
- [ ] Add deterministic report formatting tests.
