# ext2-boot BIOS Bootloader

This document captures the current Substrate `sys/boot/` BIOS bootloader architecture.

## Scope

`sys/boot/` contains the native Substrate BIOS bootloader for ext2 partitions.

## Layout

- Stage 1: `stage1.asm`
- Stage 2: `stage2.c`

## Boot Flow

- Stage 1 is a 1024-byte boot block.
- Stage 1 loads stage 2 from ext2 inode 5, collects the `boot:` command line through BIOS keyboard services while firmware USB legacy support is still active, then enters protected mode.
- Stage 2 reads the ext2 filesystem, finds `/vmunix` by name, and loads the kernel ELF image using the multiboot protocol.
- Stage 2 consumes the Stage 1 command line and keeps its protected-mode keyboard path only as a fallback.

## Build and Install

- `build-rootfs.sh --image` builds and installs the bootloader into `rootfs.img`.
- Installation uses `tools/ext2-install-boot`.

## Host Tool Requirements

- `nasm`
- `gcc -m32`
- ext2 images using 1024-byte blocks and 128-byte inodes

## Reference Material

The lazear/ext2-boot submodule that used to live at `contrib/ext2-boot` has
been removed; upstream is <https://github.com/lazear/ext2-boot> (MIT) if the
reference implementation is needed again.  This document remains as the
specification of the ext2 boot-block layout, which `tools/ext2-install-boot`
still implements for bare-ext2 images.  GRUB owns the boot path for the
partitioned `rootfs.img` layout.
