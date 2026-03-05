# 7. Core Utilities (`bin/`)

> This file was seeded from `TASKS.md` using a fork-copy (rename+restore) workflow to preserve lineage.
> Source span in original monolith: lines 8931-8948.

## Reimplemented Checklist (All Open)

### 7. Core Utilities (`bin/`)
- [ ] **Process Tools:**
    - [ ] **`ps`:**
        - [ ] **Bitness Support:** Update to receive bitness via `sys_proc_info` and format output (`-b`).
        - [ ] **Testing:** Integration tests for output verification.
- [ ] **LDT Tools:**
    - [ ] **`setldt` / `ldtctl`:** CLI tool to inspect/manipulate LDT entries.
- [ ] **Calculator Tools:**
    - [ ] **`bc`:** Standalone Interpreter with GNU Extensions (variables, control flow, libbc).
    - [ ] **`dc`:** RPN Interpreter with GNU Extensions (`#` comments, `n` command, libbc).
    - [ ] **`libbc`:** Shared Bignum Library (add, sub, mul, div, mod, pow).
- [ ] **Filesystem Tools (`sbin/`):**
    - [ ] **`mkfs`:** Implement `ext2` creation (Native Filesystem).
    - [ ] **`fsck`:** Implement `ext2` consistency check.
- [ ] **Compression Tools:**
    - [ ] **`compress`:** Implement `compress`, `uncompress`, `zcat` (LZW).
    - [ ] **`cpio`:** Implement POSIX-compatible archive utility (`usr.bin/cpio`).


## User Stories

- **US-11-0001**: As a Substrate contributor working on 7. Core Utilities (`bin/`), I want to process Tools: so that this capability is implemented with clear verification evidence.
- **US-11-0002**: As a Substrate contributor working on 7. Core Utilities (`bin/`), I want to ps: so that this capability is implemented with clear verification evidence.
- **US-11-0003**: As a Substrate contributor working on 7. Core Utilities (`bin/`), I want to bitness Support: Update to receive bitness via sys_proc_info and format output (-b) so that this capability is implemented with clear verification evidence.
- **US-11-0004**: As a Substrate contributor working on 7. Core Utilities (`bin/`), I want to testing: Integration tests for output verification so that this capability is implemented with clear verification evidence.
- **US-11-0005**: As a Substrate contributor working on 7. Core Utilities (`bin/`), I want to lDT Tools: so that this capability is implemented with clear verification evidence.
- **US-11-0006**: As a Substrate contributor working on 7. Core Utilities (`bin/`), I want to setldt / ldtctl: CLI tool to inspect/manipulate LDT entries so that this capability is implemented with clear verification evidence.
- **US-11-0007**: As a Substrate contributor working on 7. Core Utilities (`bin/`), I want to calculator Tools: so that this capability is implemented with clear verification evidence.
- **US-11-0008**: As a Substrate contributor working on 7. Core Utilities (`bin/`), I want to bc: Standalone Interpreter with GNU Extensions (variables, control flow, libbc) so that this capability is implemented with clear verification evidence.
- **US-11-0009**: As a Substrate contributor working on 7. Core Utilities (`bin/`), I want to dc: RPN Interpreter with GNU Extensions (# comments, n command, libbc) so that this capability is implemented with clear verification evidence.
- **US-11-0010**: As a Substrate contributor working on 7. Core Utilities (`bin/`), I want to libbc: Shared Bignum Library (add, sub, mul, div, mod, pow) so that this capability is implemented with clear verification evidence.
- **US-11-0011**: As a Substrate contributor working on 7. Core Utilities (`bin/`), I want to filesystem Tools (sbin/): so that this capability is implemented with clear verification evidence.
- **US-11-0012**: As a Substrate contributor working on 7. Core Utilities (`bin/`), I want to mkfs: Implement ext2 creation (Native Filesystem) so that this capability is implemented with clear verification evidence.
- **US-11-0013**: As a Substrate contributor working on 7. Core Utilities (`bin/`), I want to fsck: Implement ext2 consistency check so that this capability is implemented with clear verification evidence.
- **US-11-0014**: As a Substrate contributor working on 7. Core Utilities (`bin/`), I want to compression Tools: so that this capability is implemented with clear verification evidence.
- **US-11-0015**: As a Substrate contributor working on 7. Core Utilities (`bin/`), I want to compress: Implement compress, uncompress, zcat (LZW) so that this capability is implemented with clear verification evidence.
- **US-11-0016**: As a Substrate contributor working on 7. Core Utilities (`bin/`), I want to cpio: Implement POSIX-compatible archive utility (usr.bin/cpio) so that this capability is implemented with clear verification evidence.

## INCOSE/EARS Requirements

- **REQ-11-0001** (EARS/Ubiquitous): The Substrate system shall process Tools:.
  - Context: 7. Core Utilities (`bin/`)
  - Verification: design review + implementation evidence + test/doc update.
- **REQ-11-0002** (EARS/Ubiquitous): The Substrate system shall ps:.
  - Context: 7. Core Utilities (`bin/`)
  - Verification: design review + implementation evidence + test/doc update.
- **REQ-11-0003** (EARS/Ubiquitous): The Substrate system shall bitness Support: Update to receive bitness via sys_proc_info and format output (-b).
  - Context: 7. Core Utilities (`bin/`)
  - Verification: design review + implementation evidence + test/doc update.
- **REQ-11-0004** (EARS/Ubiquitous): The Substrate system shall testing: Integration tests for output verification.
  - Context: 7. Core Utilities (`bin/`)
  - Verification: design review + implementation evidence + test/doc update.
- **REQ-11-0005** (EARS/Ubiquitous): The Substrate system shall lDT Tools:.
  - Context: 7. Core Utilities (`bin/`)
  - Verification: design review + implementation evidence + test/doc update.
- **REQ-11-0006** (EARS/Ubiquitous): The Substrate system shall setldt / ldtctl: CLI tool to inspect/manipulate LDT entries.
  - Context: 7. Core Utilities (`bin/`)
  - Verification: design review + implementation evidence + test/doc update.
- **REQ-11-0007** (EARS/Ubiquitous): The Substrate system shall calculator Tools:.
  - Context: 7. Core Utilities (`bin/`)
  - Verification: design review + implementation evidence + test/doc update.
- **REQ-11-0008** (EARS/Ubiquitous): The Substrate system shall bc: Standalone Interpreter with GNU Extensions (variables, control flow, libbc).
  - Context: 7. Core Utilities (`bin/`)
  - Verification: design review + implementation evidence + test/doc update.
- **REQ-11-0009** (EARS/Ubiquitous): The Substrate system shall dc: RPN Interpreter with GNU Extensions (# comments, n command, libbc).
  - Context: 7. Core Utilities (`bin/`)
  - Verification: design review + implementation evidence + test/doc update.
- **REQ-11-0010** (EARS/Ubiquitous): The Substrate system shall libbc: Shared Bignum Library (add, sub, mul, div, mod, pow).
  - Context: 7. Core Utilities (`bin/`)
  - Verification: design review + implementation evidence + test/doc update.
- **REQ-11-0011** (EARS/Ubiquitous): The Substrate system shall filesystem Tools (sbin/):.
  - Context: 7. Core Utilities (`bin/`)
  - Verification: design review + implementation evidence + test/doc update.
- **REQ-11-0012** (EARS/Ubiquitous): The Substrate system shall mkfs: Implement ext2 creation (Native Filesystem).
  - Context: 7. Core Utilities (`bin/`)
  - Verification: design review + implementation evidence + test/doc update.
- **REQ-11-0013** (EARS/Ubiquitous): The Substrate system shall fsck: Implement ext2 consistency check.
  - Context: 7. Core Utilities (`bin/`)
  - Verification: design review + implementation evidence + test/doc update.
- **REQ-11-0014** (EARS/Ubiquitous): The Substrate system shall compression Tools:.
  - Context: 7. Core Utilities (`bin/`)
  - Verification: design review + implementation evidence + test/doc update.
- **REQ-11-0015** (EARS/Ubiquitous): The Substrate system shall compress: Implement compress, uncompress, zcat (LZW).
  - Context: 7. Core Utilities (`bin/`)
  - Verification: design review + implementation evidence + test/doc update.
- **REQ-11-0016** (EARS/Ubiquitous): The Substrate system shall cpio: Implement POSIX-compatible archive utility (usr.bin/cpio).
  - Context: 7. Core Utilities (`bin/`)
  - Verification: design review + implementation evidence + test/doc update.
