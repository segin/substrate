# `usr.bin/addr2line` Tasklist

Goal: implement `addr2line` using `libelfobj` + DWARF section plumbing.

- [ ] Load ELF + `.debug_*` section data through `libelfobj`.
- [ ] Implement address-to-file:line lookup for function addresses.
- [ ] Implement inlined-frame reporting mode.
- [ ] Support demangling toggle and fallback behavior.
- [ ] Handle stripped binaries and missing debug data cleanly.
- [ ] Add tests against known DWARF fixtures.
- [ ] Add tests for PIE/shared object address adjustment behavior.
