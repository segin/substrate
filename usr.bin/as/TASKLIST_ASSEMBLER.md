# `usr.bin/as` Tasklist

Goal: finish standalone assembler behavior without external syntax/tool handoff, and add first-class flat-binary output support.

---

## 1. Intel Syntax (Native, No Handoff)

### 1.1 Frontend Parsing
- [ ] Parse Intel register forms without `%` prefix.
- [ ] Parse Intel immediates without `$` prefix.
- [ ] Parse destination-first operand order for x86 integer/vector instructions.
- [ ] Parse Intel memory forms:
  - [ ] `[base]`
  - [ ] `[base + disp]`
  - [ ] `[base + index*scale + disp]`
  - [ ] Segment overrides (`gs:[...]`, etc.).
- [ ] Parse Intel size qualifiers (`byte ptr`, `word ptr`, `dword ptr`, `qword ptr`, `xmmword ptr`, `ymmword ptr`, `zmmword ptr`).

### 1.2 Lowering and Encoding
- [ ] Normalize Intel operand order to shared internal operand model.
- [ ] Ensure Intel and AT&T forms encode identical bytes for semantically equivalent instructions.
- [ ] Ensure Intel syntax path uses in-tree encoders only (no `nasm`, no `gas` handoff).
- [ ] Handle Intel-specific mnemonics/forms (`movabs`, legacy aliases, string ops).

### 1.3 Diagnostics and UX
- [ ] Report Intel syntax parse errors with file:line and clear token context.
- [ ] Distinguish unsupported Intel construct vs malformed syntax.
- [x] Add `--target-help` notes documenting Intel syntax coverage and known gaps.

### 1.4 Tests
- [x] Add dual-syntax golden tests: AT&T source and Intel source produce identical `.text` bytes.
- [ ] Add Intel memory-addressing conformance tests (base/index/scale/disp permutations).
- [ ] Add Intel size-qualifier tests that validate selected opcode width/prefix.
- [x] Add negative tests for ambiguous Intel forms with expected diagnostics.

---

## 2. `-O binary` Output Mode

### 2.1 CLI and Driver Behavior
- [x] Add `-O <format>` option handling in `as` CLI.
- [x] Default `-O elf` behavior remains unchanged.
- [x] Implement `-O binary` output mode.

### 2.2 Binary Emission Semantics
- [x] Emit raw bytes (no ELF headers/sections) for selected output image.
- [x] Define section/layout policy for binary mode:
  - [x] Section ordering.
  - [x] Gap/padding behavior.
  - [x] `.org` behavior.
- [x] Reject unresolved relocations in binary mode with explicit diagnostics.
- [x] Reject ELF-only metadata directives in binary mode where required.

### 2.3 Validation and Toolchain Integration
- [x] Add tests that verify binary output byte-for-byte against expected fixtures.
- [x] Add tests for `.org`/alignment impact on binary layout.
- [x] Add tests that ensure relocation-bearing input fails under `-O binary`.
- [x] Add integration test for boot-style image assembly using binary mode.

---

## 3. Completion Criteria
- [ ] `tests/usr.bin/as/test_unit_matrix.sh` passes.
- [ ] `tests/usr.bin/as/test_integration_matrix.sh` passes.
- [ ] New Intel and binary-mode test suites pass.
- [ ] `docs/specs/as_spec.md` and `usr.man/man1/as.1` updated to reflect final behavior.
- [ ] Remove this tasklist when all boxes are complete.
