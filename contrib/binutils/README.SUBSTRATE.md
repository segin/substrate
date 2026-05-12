# GNU binutils — Substrate patch series

This directory carries the substrate-target port of GNU binutils as a
patch series against an unmodified upstream release.  Nothing in this
directory is a vendored source file.  The full upstream tree is
fetched on demand by `./fetch.sh`, which then applies the patches
listed in `series`.

| | |
|---|---|
| Upstream version | **2.46.0** |
| Tarball SHA-256  | `d75a94f4d73e7a4086f7513e67e439e8fcdcbb726ffe63f4661744e6256b2cf2` |
| Source           | `https://ftp.gnu.org/gnu/binutils/binutils-2.46.0.tar.xz` |
| Target triples   | `i386-unknown-substrate`, `x86_64-unknown-substrate` |

## Quick start

```sh
cd contrib/binutils
./fetch.sh                                 # download + extract + patch

mkdir build-i386-substrate
cd    build-i386-substrate
../build/binutils-2.46.0/configure \
    --target=i386-unknown-substrate \
    --prefix=/opt/substrate-toolchain \
    --with-sysroot=$(realpath ../../../dist) \
    --disable-werror --disable-nls --disable-gdb \
    --disable-gdbserver --disable-sim

make -j$(nproc)
sudo make install
```

Installs `i386-unknown-substrate-{as,ld,ar,nm,objdump,objcopy,strip,
readelf,...}` under `${prefix}/bin/`.  `--with-sysroot=…/dist` points
ld at substrate's staged userland so the resulting binaries pick up
the right `crt0.o` / `libc.so.0`.

## What the patches do

| File | Touches |
|---|---|
| `0001-config-sub-substrate.patch` | accept `substrate*` as an OS suffix in `config.sub` |
| `0002-elf-common-osabi-substrate.patch` | `ELFOSABI_SUBSTRATE = 64` (arch-specific slot, doesn't collide with HSA/C6000 for EM_386 / EM_X86_64) |
| `0003-bfd-config-bfd-substrate.patch` | BFD target stanzas for i386 / x86_64 substrate (route through stock vecs) |
| `0004-gas-configure-tgt-substrate.patch` | `gas` accepts the new triple, plain ELF format |
| `0005-ld-configure-tgt-substrate.patch` | `ld` accepts the new triple, defaults to the substrate emulation |
| `0006-ld-emulparams-substrate.patch` | `elf_i386_substrate` / `elf_x86_64_substrate` emulparams — stamp PT_INTERP = `/sbin/ld.so` |
| `0007-ld-makefile-wire-substrate.patch` | hook the new emuls into `ld/Makefile.{am,in}` |

## What's deliberately deferred

- **Dedicated BFD output target.**  A real `elf32-i386-substrate` /
  `elf64-x86-64-substrate` BFD vector with `ELF_OSABI =
  ELFOSABI_SUBSTRATE` would stamp the substrate OSABI byte into the
  ELF header.  That's a C-side change in `bfd/elf32-i386.c` /
  `bfd/elf64-x86-64.c` plus `bfd/targets.c` plus the regenerated
  `bfd/configure`.  Substrate's kernel doesn't currently enforce the
  OSABI byte on exec, so the current patches leave OSABI = NONE (0)
  and rely on the e_machine + INTERP path to identify substrate
  binaries.  Add the vec when the kernel starts checking.
- **Self-hosted (native) build.**  The patches assume a cross-build
  from a Linux/BSD host.  Building binutils *on* substrate needs
  more userland in place (bash, perl, etc.) than currently exists.

## Re-vendoring upstream

Bumping to a newer binutils release:

1. Edit `VERSION` and `SHA256` in `fetch.sh`.
2. Re-run `./fetch.sh` — patches probably won't apply cleanly.
3. For each rejected hunk: rebase the corresponding patch against
   the new upstream source.  The "substrate stanza" insertions live
   beside the FreeBSD/NetBSD/Redox stanzas in every file, so
   visually finding the right context is straightforward.
4. Re-run `fetch.sh` until all patches apply, then `make` to verify.

If upstream restructures `ld/Makefile.{am,in}` significantly,
re-generating patch `0007` is faster than rebasing the unified diff.

## Smoke test

After `make install`:

```sh
cat > /tmp/hello.s <<EOF
.text
.globl _start
_start:
    mov \$0, %eax    # SYS_EXIT
    mov \$42, %ebx   # exit code
    int \$0x80
EOF

i386-unknown-substrate-as -o /tmp/hello.o /tmp/hello.s
i386-unknown-substrate-ld -pie -o /tmp/hello /tmp/hello.o
file /tmp/hello
# → ELF 32-bit LSB pie executable, Intel 80386, ..., interpreter /sbin/ld.so

readelf -h /tmp/hello | grep OS/ABI
# → OS/ABI:    UNIX - System V         (until the dedicated BFD vec lands)
```
