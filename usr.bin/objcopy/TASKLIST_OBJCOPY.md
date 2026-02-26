# `usr.bin/objcopy` Tasklist

Goal: implement `objcopy` transformations through `libelfobj` object model/writer.

- [ ] Read/write objects exclusively through `libelfobj` APIs.
- [ ] Implement section copy/remove/rename/set-flags operations.
- [ ] Implement symbol strip/localize/keep/rename operations.
- [ ] Implement binary<->ELF conversion modes needed by build flow.
- [ ] Preserve alignment/flags/relocation integrity on transforms.
- [ ] Add deterministic output mode.
- [ ] Add tests for ET_REL and ET_DYN transformation safety.
- [ ] Add tests for relocation/symbol consistency post-transform.
