# Substrate-Native GNU Toolchain

Substrate builds and ships its own GNU toolchain (binutils 2.46.0 +
GCC 16.1.0), patched for the `i386-unknown-substrate` target.  This
document covers the build stages, the ELFOSABI branding, and the
image bootstrap chain.

For a one-line status, see `AGENTS.md` (Current Capabilities →
Toolchain).  Patch series live under `contrib/{binutils,gcc}/patches/`
and are reapplied by each port's `fetch.sh`; nothing in
`contrib/*/build/` is vendored.

## Build stages

- **Stage 1 (cross):** binutils 2.46.0 + GCC 16.1.0 patched for the
  substrate target, built on a Linux host.  Installs into
  `/opt/substrate` (`STAGE1_PREFIX`) as
  `i386-unknown-substrate-{gcc,g++,as,ld,ar,nm,objdump,...}`.  Used to
  cross-compile the substrate userland.
- **Stage 2 (Canadian cross):** the same binutils + GCC sources, built
  *with* the stage-1 cross compiler to produce substrate-ELF binaries
  that run *on* substrate itself.  Installs into
  `dist-overlay/dist-toolchain/usr/` (binutils) and
  `/tmp/gcc-stage2-staging/usr/` (gcc) as:
  - `/usr/bin/{gcc,g++,ld,as,ar,nm,objdump,readelf,strip,ranlib,size,strings,addr2line,c++filt,elfedit,gprof,ld.bfd}`
  - `/usr/libexec/gcc/i386-unknown-substrate/16.1.0/{cc1,cc1plus,lto1,lto-dump,collect2,lto-wrapper}`
  - `/usr/lib/gcc/i386-unknown-substrate/16.1.0/{libgcc.a,libgcov.a,crtbegin*.o,crtend*.o}`

  On the image, `cc` is a symlink to `gcc`.

## Bootstrap orchestrator

`contrib/build-toolchain.sh` drives all four phases (binutils stage 1,
gcc stage 1, binutils stage 2, gcc stage 2) with idempotent fetch +
patch + build.  The per-component scripts `contrib/binutils/build.sh`
and `contrib/gcc/build.sh` can also be run individually.

- `contrib/binutils/` adds the `elf32-i386-substrate` /
  `elf64-x86-64-substrate` BFD output vecs, the `elf_i386_substrate`
  ld emulation, the `ELFOSABI_SUBSTRATE` constant in
  `include/elf/common.h`, and readelf's name resolution for OSABI=64.
- `contrib/gcc/` configures `i386-unknown-substrate` as a target and a
  libstdc++-v3 OS port at `libstdc++-v3/config/os/substrate/`.

## ELFOSABI_SUBSTRATE = 64

An architecture-specific OSABI byte stamped by every substrate-target
BFD output vec (`elf32-i386-substrate`, `elf64-x86-64-substrate`).
The vec has `ELF_OSABI_EXACT = 0` so it accepts SysV (OSABI=0) input
objects during bootstrap, but every executable / DSO it produces
carries OSABI=64.  This is the wire-level identifier the kernel exec
personality dispatch uses to route a binary to its loader.

Not yet done: `gas` still emits `ELFOSABI_SYSV` (0) on output `.o`
files; we sidestep it with `ELF_OSABI_EXACT=0` in the substrate BFD
vec.  The real fix belongs in `gas/config/obj-elf.c` (or a new
`gas/config/te-substrate.h`).  See `AGENTS.md` → Next Steps.

## C++ cross-DSO exceptions

FIXED and verified end-to-end.  Substrate's gcc statically linked
libgcc into every module (each with its own DWARF FDE registry) and
libgcc was never built to use `dl_iterate_phdr`, so a C++ exception
thrown inside a shared library could not unwind back to its caller —
it hit `terminate()`/abort (breaking TagLib/PsyMP3 and the whole C++
desktop story).

Fixed by `contrib/gcc/patches/0010-libgcc-pt-gnu-eh-frame-substrate.patch`:

- `USE_PT_GNU_EH_FRAME` for `__substrate__` in libgcc
  (`unwind-dw2-fde-dip.c` / `crtstuff.c`);
- `--eh-frame-hdr` via `LINK_EH_SPEC` (the canonical `%(link_eh)`
  hook — `LINK_SPEC` did not reach ld);
- `t-slibgcc` so g++ links the shared `libgcc_s.so`;
- `thread_file=posix` so libstdc++ has `std::mutex`.

Userspace half: `dl_iterate_phdr(3)` in ld.so + libc, and
`Elf32_Dyn`/`DT_*` in `<elf.h>`.  Verified: a throw in a `.so` is
caught in the exe (rc=0), and TagLib reads a FLAC's metadata instead
of aborting.

Follow-up (still true): substrate's gthr-posix uses hard (non-weak)
pthread refs, so C++ programs using `std::mutex` must link
`-lpthread` (the gcc driver should auto-link it).

## gdb

`gdb` (`contrib/gdb/`) runs on substrate end-to-end: the libsys
`ptrace` PEEK bridge (`lib/sys/ptrace.c`) backs the debugger's memory
reads, and `gdb` is stripped during its build (`contrib/gdb/build.sh`).

## Image bootstrap chain

Committed end-to-end:

```
contrib/build-toolchain.sh        # stage 1 cross + stage 2 native
./build-rootfs.sh --dist          # substrate userland into dist/
./build-rootfs.sh --toolchain     # overlay stage-2 toolchain
./build-rootfs.sh --image         # bake 4 GiB rootfs.img
```

Or the all-in-one orchestrator that drives all of the above plus every
contrib port in dependency order:

```
./build.sh                        # repo-root, from clean checkout (no root)
                                  # env knobs: SKIP_TOOLCHAIN,
                                  # SKIP_CONTRIB, SKIP_IMAGE,
                                  # ONLY="pkg1 pkg2 ..."
```

After stage 1 and after each contrib build, `build.sh` mirrors the
produced libs + headers into the cross-toolchain sysroot at
`${STAGE1_PREFIX}/i386-unknown-substrate/{lib,include}` and the
matching gcc include-fixed snapshot, so the next layer's configure
probes find them.
