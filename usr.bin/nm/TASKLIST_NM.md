# `usr.bin/nm` Tasklist

Goal: implement `nm` with ELF parsing exclusively via `libelfobj`.

- [ ] Read symbol tables (`.symtab`/`.dynsym`) through `libelfobj` only.
- [ ] Implement standard output modes (`-n`, `-p`, `-u`, `-g`, `-a`).
- [ ] Correctly classify symbol types/bind/visibility.
- [ ] Handle weak/local/global/versioned symbols.
- [ ] Support archive input by iterating members and reusing `libelfobj` parsing.
- [ ] Add stable sort behavior and deterministic output formatting.
- [ ] Add diagnostics for malformed symbols/sections.
- [ ] Add tests for relocatable/shared/executable inputs.
- [ ] Add tests for symbol versioning and TLS symbols.
