# `usr.bin/ar` Tasklist

Goal: implement a production `ar` where ELF member inspection/parsing comes from `libelfobj` only.

- [ ] Route ELF symbol/member inspection through `libelfobj` APIs only.
- [ ] Support create/replace/delete/extract/list operations (`r`, `d`, `x`, `t`, `q`).
- [ ] Support deterministic mode and stable archive metadata ordering.
- [ ] Implement GNU/BSD archive variants needed by the tree.
- [ ] Implement long filename table handling and edge cases.
- [ ] Preserve file modes/timestamps/uid/gid policy options.
- [ ] Add robust malformed archive diagnostics.
- [ ] Add unit tests for mixed ELF32/ELF64 members.
- [ ] Add regression tests for duplicate member names and replacement behavior.
- [ ] Add integration tests with `cc/as/ld` build outputs.
