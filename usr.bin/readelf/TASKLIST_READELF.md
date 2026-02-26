# `usr.bin/readelf` Tasklist

Goal: implement `readelf` entirely on `libelfobj` for ELF semantics.

- [ ] Dump ELF header/class/endian/machine/type via `libelfobj`.
- [ ] Dump program headers and section headers.
- [ ] Dump symbols, relocations, dynamic tags, notes, versions.
- [ ] Dump string tables with bounds-safe formatting.
- [ ] Support GNU/BSD extension sections and unknown-section fallback.
- [ ] Match expected option surface (`-h`, `-l`, `-S`, `-s`, `-r`, `-d`, `-n`, `-V`).
- [ ] Add robust malformed input diagnostics.
- [ ] Add round-trip/output comparison tests vs known-good fixtures.
