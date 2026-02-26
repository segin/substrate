# `usr.bin/size` Tasklist

Goal: implement `size` using section accounting from `libelfobj`.

- [ ] Compute text/data/bss sizes from ELF sections via `libelfobj`.
- [ ] Support SysV/Berkeley output formats.
- [ ] Support archive input and per-member reporting.
- [ ] Handle ELF32/ELF64 totals without overflow.
- [ ] Add deterministic sorting/formatting.
- [ ] Add tests for ET_REL/ET_EXEC/ET_DYN sizing correctness.
