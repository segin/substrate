# `usr.bin/objdump` Tasklist

Goal: implement `objdump` with ELF structure loading from `libelfobj`.

- [ ] Use `libelfobj` for ELF headers, sections, symbols, relocations, notes.
- [ ] Implement section/header dumps (`-h`, `-x`, `-s`) with stable formatting.
- [ ] Implement symbol/relocation displays (`-t`, `-r`, `-R`).
- [ ] Implement disassembly pipeline hook (`-d`, `-D`) with arch backend interface.
- [ ] Correlate disassembly with symbols and relocations.
- [ ] Support archive traversal and per-member output banners.
- [ ] Add diagnostics for unsupported machine/class combinations.
- [ ] Add tests for ET_REL, ET_EXEC, ET_DYN inputs.
- [ ] Add tests for malformed/truncated binaries.
