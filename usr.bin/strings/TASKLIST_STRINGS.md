# `usr.bin/strings` Tasklist

Goal: implement `strings` with optional ELF-aware scanning driven by `libelfobj`.

- [ ] Implement raw byte scanning mode with min-length/encoding options.
- [ ] Add ELF-aware mode that scans alloc/progbits sections via `libelfobj`.
- [ ] Support file offset/address prefix output modes.
- [ ] Handle UTF-8 and byte-string modes consistently.
- [ ] Add deterministic ordering of extracted strings.
- [ ] Add malformed ELF handling with clear diagnostics.
- [ ] Add tests for ELF/non-ELF files and mixed binary blobs.
