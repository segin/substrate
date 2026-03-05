# 11. Build System & Packaging

> This file was seeded from `TASKS.md` using a fork-copy (rename+restore) workflow to preserve lineage.
> Source span in original monolith: lines 9644-9651.

## Reimplemented Checklist (All Open)

### 11. Build System & Packaging
- [ ] **Kernel Image Formats:**
    - [ ] **zImage Support:**
        - [ ] Standard Linux-compatible zImage header (magic, entry point, size).
        - [ ] Flat binary generation (`objcopy -O binary`).
        - [ ] Bootloader compatibility (QEMU `-kernel` or U-Boot `bootz`).
        - [ ] Piggybacked initrd support (if applicable).


## User Stories

- **US-15-0001**: As a Substrate contributor working on 11. Build System & Packaging, I want to kernel Image Formats: so that this capability is implemented with clear verification evidence.
- **US-15-0002**: As a Substrate contributor working on 11. Build System & Packaging, I want to zImage Support: so that this capability is implemented with clear verification evidence.
- **US-15-0003**: As a Substrate contributor working on 11. Build System & Packaging, I want to standard Linux-compatible zImage header (magic, entry point, size) so that this capability is implemented with clear verification evidence.
- **US-15-0004**: As a Substrate contributor working on 11. Build System & Packaging, I want to flat binary generation (objcopy -O binary) so that this capability is implemented with clear verification evidence.
- **US-15-0005**: As a Substrate contributor working on 11. Build System & Packaging, I want to bootloader compatibility (QEMU -kernel or U-Boot bootz) so that this capability is implemented with clear verification evidence.
- **US-15-0006**: As a Substrate contributor working on 11. Build System & Packaging, I want to piggybacked initrd support (if applicable) so that this capability is implemented with clear verification evidence.

## INCOSE/EARS Requirements

- **REQ-15-0001** (EARS/Ubiquitous): The Substrate system shall kernel Image Formats:.
  - Context: 11. Build System & Packaging
  - Verification: design review + implementation evidence + test/doc update.
- **REQ-15-0002** (EARS/Ubiquitous): The Substrate system shall zImage Support:.
  - Context: 11. Build System & Packaging
  - Verification: design review + implementation evidence + test/doc update.
- **REQ-15-0003** (EARS/Ubiquitous): The Substrate system shall standard Linux-compatible zImage header (magic, entry point, size).
  - Context: 11. Build System & Packaging
  - Verification: design review + implementation evidence + test/doc update.
- **REQ-15-0004** (EARS/Ubiquitous): The Substrate system shall flat binary generation (objcopy -O binary).
  - Context: 11. Build System & Packaging
  - Verification: design review + implementation evidence + test/doc update.
- **REQ-15-0005** (EARS/Ubiquitous): The Substrate system shall bootloader compatibility (QEMU -kernel or U-Boot bootz).
  - Context: 11. Build System & Packaging
  - Verification: design review + implementation evidence + test/doc update.
- **REQ-15-0006** (EARS/Ubiquitous): The Substrate system shall piggybacked initrd support (if applicable).
  - Context: 11. Build System & Packaging
  - Verification: design review + implementation evidence + test/doc update.
