# `usr.bin/ranlib` Tasklist

Goal: implement `ranlib` index generation using `libelfobj` for ELF symbol extraction.

- [ ] Build archive symbol table (`__.SYMDEF` / variant) using symbols read via `libelfobj`.
- [ ] Support deterministic index generation.
- [ ] Preserve compatibility with `ar` archive formats used in-tree.
- [ ] Ensure index updates are atomic on failure.
- [ ] Emit clear diagnostics for invalid members and truncated archives.
- [ ] Add tests for archives containing ELF32 + ELF64 objects.
- [ ] Add tests for empty archives and archives with no global symbols.
- [ ] Add integration test with linker symbol lookup from archives.
