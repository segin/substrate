# x86_64 Boot Protocol

How the 64-bit kernel is entered.  The full bring-up, memory layout and
selector contract are in `docs/specs/arch_x86_64_core.md`.  The
system-call interface programs see is in `docs/specs/abi-amd64.md`.

## Images

| File | Header | Loader |
| :--- | :----- | :----- |
| `sys/kernel-x86_64.bin` (`/vmunix64`) | multiboot 1, a.out kludge | QEMU `-kernel`, GRUB `multiboot` |
| `sys/kernel-x86_64.elf` | multiboot 2 | GRUB `multiboot2` |

Multiboot 1 loaders refuse ELF64 files, so the flat image's header gives
the load addresses itself: `load_addr` 1 MiB, `load_end_addr` the end of
`.data`, `bss_end_addr` the end of `.bss` (zeroed by the loader),
`entry_addr` the physical address of `_start`.

## Entry state

Either protocol: 32-bit protected mode, paging off, flat segments, `%eax`
the loader magic (`0x2BADB002` or `0x36D76289`), `%ebx` the physical
address of the information structure.  `_start` switches to long mode
itself and calls `kmain64(magic, info)`, which accepts both formats.

## Layout

* Physical load address: 1 MiB (`KERNEL_LOAD_PHYS`).
* Link address: `0xFFFFFFFF80000000` + 1 MiB (`KERNEL_VMA`).
* Direct map of physical memory at `0xFFFFF80000000000` (`DMAP_BASE`).

All three are in `layout.h`.

## EFI

`efi/` holds the start of a native PE32+ entry (`efi_main`); it is not
built yet.
