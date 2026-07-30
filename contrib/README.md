# contrib/ — third-party software ports

This directory holds substrate's ports of upstream software.  Nothing
in here is vendored — each subdirectory carries the scripts and
patches needed to **fetch the upstream tarball, verify its PGP
signature, apply the substrate patches, and cross-build for substrate
target**.  The build outputs land in `dist-<name>/usr/{bin,lib,
include}` and are picked up by `build-rootfs.sh`'s contrib-overlay
loop.

## Layout

Each port follows the same shape:

```
contrib/<name>/
  fetch.sh           download + PGP-verify + extract + apply patches
  build.sh           cross-build with the stage-1 toolchain
  series             ordered list of patches in patches/
  patches/           the substrate patches (committed; upstream isn't)
  .gitignore         excludes build/  (where fetch.sh + build.sh work)
  README.SUBSTRATE.md  optional per-port notes
```

`fetch.sh --no-network` re-applies patches against the already-
downloaded tarball — useful after editing a patch.

## Current ports

### Toolchain (substrate-native GNU toolchain)

| dir | upstream | role |
|-----|----------|------|
| `binutils/` | binutils 2.46.0 (ftp.gnu.org) | adds `elf32-i386-substrate` / `elf64-x86-64-substrate` BFD output vecs, `elf_i386_substrate` ld emulation, `ELFOSABI_SUBSTRATE = 64` in `include/elf/common.h`, readelf OSABI-64 name resolution |
| `gcc/` | GCC 16.1.0 (ftp.gnu.org) | adds `i386-unknown-substrate` and `x86_64-unknown-substrate` target triples, libstdc++-v3 OS port at `libstdc++-v3/config/os/substrate/` |
| `build-toolchain.sh` | (orchestrator) | drives binutils stage 1 → gcc stage 1 → binutils stage 2 → gcc stage 2 in order.  Stage 1 = cross compiler on the Linux build host (`/opt/substrate/bin/i386-unknown-substrate-{gcc,as,ld,...}`); stage 2 = Canadian cross producing substrate-ELF binaries that run *on* substrate (`/usr/bin/{gcc,as,ld,...}` inside the image). |
| `BUILD-TOOLCHAIN.md` | docs | high-level walkthrough of the four-phase bootstrap. |

### Userland — fully built and shipped

| dir | upstream | what lands on the image |
|-----|----------|------------------------|
| `bzip2/` | bzip2 1.0.8 (sourceware.org) | `/usr/bin/{bzip2,bunzip2,bzcat,bzip2recover}` + `libbz2.{a,so.1.0.8}` + `bzlib.h` |
| `curl/` | curl 8.7.1 (curl.se) | `/usr/bin/curl` + `libcurl` (links against substrate's OpenSSL port for TLS) |
| `gzip/` | gzip 1.13 (ftp.gnu.org) | `/usr/bin/{gzip,gunzip,zcat,zless,...}` |
| `inetutils/` | inetutils 2.5 (ftp.gnu.org) | `/usr/bin/telnet`, `/usr/libexec/{inetd,telnetd}` |
| `libarchive/` | libarchive 3.7.7 (libarchive.org) | `/usr/bin/{bsdtar,tar→bsdtar}`.  Replaces the buggy in-tree `bin/tar`.  Bzip2 backend lights up when `dist-bzip2/` is present; zlib / xz / zstd backends pending those ports. |
| `mpg123/` | mpg123 1.32.10 (mpg123.de) | `/usr/bin/mpg123` |
| `openssl/` | OpenSSL 3.0.13 (openssl.org) | `/usr/lib/{libssl.so.3,libcrypto.so.3}` + `/usr/bin/openssl` |

### Bootloader / data

| dir | what it is |
|-----|-----------|
| `grub/` | GNU GRUB 2.12.  **Host-tool port**, not Substrate userland: the bootloader that loads the kernel plus the utilities that bake it into `rootfs.img` (`grub-mkimage`, `grub-file`, `grub-mkrescue`) and the `i386-pc` / `x86_64-efi` / `i386-efi` module trees.  `build-rootfs.sh` and `mkgrub.sh` prefer it over the host GRUB.  See `README.SUBSTRATE.md`. |
| `tzdata/` | IANA tzdata 2024a; ships zone files under `/usr/share/zoneinfo` for libc's tz parsing. |

### Skeletons — fetched but not yet building

| dir | upstream | status |
|-----|----------|--------|
| `libiconv/` | GNU libiconv 1.17 | tree exists, build not yet plumbed.  Needed once libarchive's iconv-using paths matter. |
| `make/` | GNU make 4.4.1 | tree exists, build not yet plumbed.  Useful once substrate self-hosts a Makefile-driven build. |
| `onetrueawk/` | Brian Kernighan's awk | source tree present, no fetch/build scripts yet. |

## ABI conventions

- **ELFOSABI_SUBSTRATE = 64** is the wire-level identifier for any
  binary or DSO produced by the substrate-target toolchain.
  binutils' `elf32-i386-substrate` / `elf64-x86-64-substrate` BFD
  output vecs stamp it.  Each contrib `build.sh` also post-patches
  every produced `*.so.*.*` with `dd bs=1 seek=7 count=1` to set the
  OSABI byte — substrate's cross-ld stamps `ELFOSABI_SYSV` (0) on
  shared output, which the kernel exec-personality dispatch rejects
  as "file in wrong format".
- **Syscall ABI**: `int $0x80`.  Substrate-native, FreeBSD, NetBSD,
  OpenBSD, and SVR4 personalities all use BSD-style stack-based args
  (number in `%eax`, args on the user stack with a dummy return slot
  at `[esp+0]`).  Linux personality uses register-passing
  (`%eax / %ebx / %ecx / %edx / %esi / %edi / %ebp`).  ELKS uses the
  same registers but 16-bit-truncated.
- **Build environment**: ports cross-build with the stage-1 toolchain
  on the Linux developer host.  `STAGE1_PREFIX` (default
  `/opt/substrate`) holds the cross gcc/binutils.  `DESTDIR` (default
  `${SUBSTRATE_TOP}/dist-overlay/dist-<name>`) is where the build's `make install`
  stages the result.  `build-rootfs.sh --image` overlays each
  `dist-<name>/` into the rootfs image in alphabetical order.

## Adding a new port

1. `mkdir contrib/<name>/{patches,build}` and write `.gitignore`
   containing `build/` and `build-*/`.
2. Write `fetch.sh` modelled on an existing port — set `VERSION`,
   `URL`, `SHA256`, and the path to the upstream's PGP signature.
   Cross-verify the SHA256 against the signature by hand at version-
   bump time; the gpg call in fetch.sh is a re-check, not the
   primary trust root.
3. Write `series` (one patch filename per line) and put the patches
   under `patches/`.  The first patch is almost always
   `0001-config-sub-substrate.patch` to teach autoconf about the
   `i386-unknown-substrate` triple.
4. Write `build.sh`: source the orchestration boilerplate, invoke
   the upstream configure with `--host=i386-unknown-substrate
   --build=x86_64-pc-linux-gnu`, disable optional features whose
   dependencies haven't landed (use `--without-<x>`), and install
   into `${DESTDIR}/usr/`.
5. Add `<name>` to the contrib-overlay loop in `build-rootfs.sh`
   (the `for ov in inetutils openssl curl gzip bzip2 libarchive`
   list).
