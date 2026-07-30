# GRUB 2.12 — Substrate port

Vendors GNU GRUB so the bootable image does not depend on whatever GRUB the
build host happens to have installed (or on it being installed at all).

```sh
./fetch.sh          # download + SHA256/PGP verify + extract + patch
./build.sh          # build all three platforms, stage into dist-grub/usr
./build.sh i386-pc  # or just one
```

## What this port is, and is not

This is the one **host-tool** port in `contrib/`. Everything else here is
Substrate userland cross-compiled for `i386-unknown-substrate`; GRUB is not.
It is the bootloader that *loads* the Substrate kernel, plus the host
utilities that assemble it into `rootfs.img`. Nothing built here runs under
Substrate, so it builds with the plain host compiler and deliberately does
**not** use the Substrate cross toolchain.

## Platforms

GRUB configures for exactly one target platform per pass, so `build.sh` runs
one pass per platform into its own `build/obj-<platform>` directory. All three
install into the same prefix: platform modules land in distinct
`lib/grub/<target>-<platform>/` trees, and the host utilities are identical
across passes, so later passes simply overwrite them.

| Platform | Consumed by | Produces |
|---|---|---|
| `i386-pc` | `build-rootfs.sh install_grub`, `tools/grub-embed-mbr` | `boot.img` (goes in the MBR), `core.img` (post-MBR gap), `*.mod` |
| `x86_64-efi` | `install_grub` | `/EFI/BOOT/BOOTX64.EFI` |
| `i386-efi` | `install_grub` | `/EFI/BOOT/BOOTIA32.EFI`, for 32-bit UEFI firmware |

The i386 platforms build with the host compiler in 32-bit mode (`-m32`). GRUB
is freestanding, so no 32-bit userland runtime is needed — but the compiler
must still be able to emit 32-bit objects, and `build.sh` checks that up front
and skips the platform with a clear message rather than failing obscurely.

## How the build finds it

`build-rootfs.sh` and `mkgrub.sh` both prefer `dist-grub/usr` and fall back to
the host install when this port has not been built:

- `build-rootfs.sh` — `grub_setup()` resolves `GRUB_BIN`/`GRUB_LIB` and prints
  which one it used (`GRUB: contrib/grub (vendored 2.12)` vs `GRUB: host`).
  Every `grub-mkimage` call and every module-tree path goes through those.
- `mkgrub.sh` — resolves `grub-mkrescue` and `grub-file`, passing
  `-d dist-grub/usr/lib/grub` so `grub-mkrescue` finds the vendored modules.

So a host with no GRUB installed can still bake a bootable image, and a host
*with* GRUB installed is no longer silently used in preference to ours.

## Build quirk worth knowing

The 2.12 release tarball does not ship `grub-core/extra_deps.lst`, but the
generated Makefile depends on it:

```make
syminfo.lst: gensyminfo.sh kernel_syms.lst $(top_srcdir)/grub-core/extra_deps.lst ...
        cat kernel_syms.lst $(top_srcdir)/grub-core/extra_deps.lst > $@.new
```

An out-of-tree build therefore dies with `No rule to make target
.../grub-core/extra_deps.lst`. It is a list of extra inter-module dependencies
appended to `kernel_syms.lst`; we add none, so an empty file is the correct
content. `build.sh` creates it if absent. This lives in `build.sh` rather than
`patches/` on purpose: it is a missing *generated* file, not a defect in
upstream source.

## Verification performed

- Tarball SHA256 cross-checked against upstream's detached PGP signature:
  good signature from Daniel Kiper `<dkiper@net-space.pl>`, RSA key
  `BE5C23209ACDDACEB20DB0A28C8189F1988C2166`.
- All three platforms build: 273 modules (i386-pc), 268 (x86_64-efi),
  269 (i386-efi), plus `grub-mkimage`, `grub-file`, `grub-mkrescue`.
- `grub-file --is-x86-multiboot sys/kernel.bin` and `--is-x86-multiboot2
  sys/kernel.fb.bin` both pass, i.e. the vendored parser accepts our kernels.
- `grub-mkimage` produces a real `PE32+ executable for EFI (application)` for
  x86_64-efi and a 151 KiB `core.img` for i386-pc; `boot.img` is 512 bytes.
- End to end: embedded the vendored `boot.img`/`core.img` into a copy of
  `rootfs.img` with `tools/grub-embed-mbr`, refreshed the ESP's `i386-pc`
  modules from the vendored tree, and booted it under QEMU — BIOS reached
  GRUB, GRUB multibooted `/vmunix`, kernel came up to `KMAIN: vm ready` with
  no panic.

## Not committed

`build/` (tarball, extracted tree, per-platform object dirs) and `dist-grub/`
are build products and are gitignored — the repo carries the scripts and the
patch series, never upstream source. Run `./fetch.sh && ./build.sh` to
reproduce.
