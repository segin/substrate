# `usr.bin/elfedit` Tasklist

Goal: implement `elfedit` mutations on top of `libelfobj`.

- [ ] Use `libelfobj` read/modify/write paths exclusively.
- [ ] Implement safe edits for ELF header fields (type, machine, flags, entry).
- [ ] Implement safe edits for section/program header attributes.
- [ ] Validate all edits with `elf_validate` before write-out.
- [ ] Provide dry-run mode and explicit unsafe-operation guardrails.
- [ ] Add tests for legal/illegal edit combinations.
