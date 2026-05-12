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
| OSABI byte       | `ELFOSABI_SUBSTRATE = 64`, stamped in `e_ident[EI_OSABI]` |

## Quick start

```sh
cd contrib/binutils
./fetch.sh                              # download + sha256 + extract + patch
./build.sh --stage=1                    # build the cross-toolchain
```

`build.sh` is the recommended entry point — it locates the patched
tree under `build/`, configures it correctly against the substrate
sysroot, and installs to `$STAGE1_PREFIX` (default
`/opt/substrate-toolchain`).  See `--help` and the env knobs at the
top of the script.

## Two build stages

`build.sh --stage=1`
:   **Cross-toolchain.** build = host = Linux; target = substrate.
    Produces `i386-unknown-substrate-{as,ld,ar,nm,objdump,readelf,
    strip,...}` running on the Linux build host, emitting substrate
    ELFs.  This is the toolchain you use from a Linux box to cross-
    compile substrate userland.

`build.sh --stage=2`
:   **Native-on-substrate (Canadian cross).** build = Linux;
    host = target = substrate; `--prefix=/usr`.  Compiles binutils
    itself as substrate ELFs using the stage-1 cross gcc, installs
    into a staging `DESTDIR` so the result can be dropped into
    `rootfs.img` (substrate's `/usr/bin/as`, `/usr/bin/ld`, etc.).
    **Currently parked**: needs `contrib/gcc/` patches (not yet
    vendored) to provide a substrate-target C compiler.

Both stages are orchestrated by `../build-toolchain.sh`, which loops
over `contrib/{binutils,gcc}/` and runs every component through every
requested stage in the right order.

## What the patches do

| # | Touches | Purpose |
|---|---|---|
| 0001 | `config.sub` | accept `substrate*` as an OS suffix |
| 0002 | `include/elf/common.h` | `ELFOSABI_SUBSTRATE = 64` (arch-specific slot; same numeric value as ELFOSABI_AMDGPU_HSA on EM_AMDGPU and ELFOSABI_C6000_ELFABI on EM_TI_C6000 but the spec scopes 64..255 per e_machine, so no real collision for EM_386 / EM_X86_64) |
| 0003 | `bfd/elf32-i386.c`, `bfd/elf64-x86-64.c` | new BFD output vecs `elf32-i386-substrate` / `elf64-x86-64-substrate` that stamp `EI_OSABI = ELFOSABI_SUBSTRATE` (64) into e_ident |
| 0004 | `bfd/targets.c` | forward-declare + add the new vecs to the default vector list |
| 0005 | `bfd/configure.ac`, `bfd/configure` | link the substrate vec backends (parallel edit to both autotools input and regenerated output, so re-running autoreconf doesn't lose the change) |
| 0006 | `bfd/config.bfd` | i386/x86_64-substrate stanzas now use the new substrate vecs as defvecs |
| 0007 | `gas/configure.tgt` | gas accepts the new triple, plain ELF format |
| 0008 | `ld/configure.tgt` | ld accepts the new triple; default emul = `elf_{i386,x86_64}_substrate` |
| 0009 | `ld/emulparams/elf_{i386,x86_64}_substrate.sh` | new emuls — set `OUTPUT_FORMAT` to the OSABI-stamping vec and `ELF_INTERPRETER_NAME = /sbin/ld.so` |
| 0010 | `ld/Makefile.am`, `ld/Makefile.in` | hook the new emuls into the build |
| 0011 | `binutils/readelf.c` | pretty-print `OS/ABI: Substrate` instead of `<unknown: 40>` for substrate ELFs |

## What's deliberately deferred

- **Stage 2 native build.**  Substrate-ELF binutils that run on the
  target — uses Canadian cross via stage-1 gcc.  Blocked on
  `contrib/gcc/` (not yet vendored).  `build.sh --stage=2` is
  wired up and will work the moment a substrate-target `gcc` is
  on PATH.

- **Self-hosted (native) build.**  Building binutils *on* substrate
  needs much more userland (bash, perl, make) than currently exists.
  Canadian cross from Linux suffices for now.

## Re-vendoring upstream

Bumping to a newer binutils release:

1. Edit `VERSION` and `SHA256` in `fetch.sh`.
2. Remove the stale tree:  `rm -rf build/binutils-*/`
3. Re-run `./fetch.sh` — patches probably won't apply cleanly against
   the new upstream.
4. For each rejected hunk: rebase the corresponding patch against the
   new upstream source.  Substrate stanzas live beside the FreeBSD /
   NetBSD / Redox stanzas in every file, so visually finding the
   right context is straightforward.
5. Re-run `fetch.sh` until all patches apply, then `build.sh
   --stage=1` to verify the cross-toolchain still builds.

If upstream restructures `ld/Makefile.{am,in}` significantly,
regenerating patch `0010` from a diff between pristine and a working
tree is faster than rebasing.

## Smoke test (after `build.sh --stage=1`)

```sh
PATH=/opt/substrate-toolchain/bin:$PATH

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

i386-unknown-substrate-readelf -h /tmp/hello | grep OS/ABI
# → OS/ABI: Substrate

i386-unknown-substrate-objdump -p /tmp/hello | head -1
# → file format elf32-i386-substrate

od -An -tx1 -N16 /tmp/hello | head -1
# →  7f 45 4c 46 01 01 01 40 00 00 00 00 00 00 00 00
#                        ^^ EI_OSABI = ELFOSABI_SUBSTRATE = 64
```
