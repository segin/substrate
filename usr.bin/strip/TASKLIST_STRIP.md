# `usr.bin/strip` Tasklist

Goal: implement `strip` as a constrained `libelfobj` transform.

- [ ] Remove debug/non-essential symbols via `libelfobj` symbol APIs.
- [ ] Preserve required dynamic symbols/relocations for ET_DYN/ET_EXEC.
- [ ] Support common modes (`--strip-all`, `--strip-debug`, keep-list).
- [ ] Maintain section/program-header consistency after stripping.
- [ ] Add deterministic output behavior.
- [ ] Add tests for executable/shared object runtime integrity.
- [ ] Add tests for relocatable object strip behavior.
