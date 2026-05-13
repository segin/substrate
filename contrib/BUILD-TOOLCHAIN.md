# Substrate Toolchain — Build & Install Guide

End-to-end documentation for producing the substrate-target binutils
and gcc, both as a **stage-1 cross-toolchain** that runs on the build
host and as a **stage-2 native toolchain** installed inside the
substrate rootfs.

Treat this file as the authoritative map; the per-component
`contrib/binutils/README.SUBSTRATE.md` and `contrib/gcc/README.SUBSTRATE.md`
go into the per-component details.

## TL;DR

```sh
# One-shot: fetch + patch + build everything that's wired up.
cd contrib
./build-toolchain.sh
```

Default install location: `/opt/substrate/`.  Override with
`STAGE1_PREFIX=/some/other/path`.  Stage 1 needs write access to that
prefix (use `sudo mkdir -p /opt/substrate && sudo chown $USER:$GROUP
/opt/substrate` once).

## Repository layout

```
contrib/
├── BUILD-TOOLCHAIN.md          # this file — top-level guide
├── build-toolchain.sh          # orchestrator: loops over components
├── README.md                   # historical target-triple notes
│
├── binutils/
│   ├── README.SUBSTRATE.md     # binutils-specific notes
│   ├── fetch.sh                # download + sha256 + patch
│   ├── build.sh                # --stage=1 | --stage=2
│   ├── series                  # ordered patch list
│   ├── patches/                # 11 substrate patches
│   ├── build/                  # extracted upstream tree (gitignored)
│   ├── build-stage1/           # stage-1 build dir (gitignored)
│   └── build-stage2/           # stage-2 build dir (gitignored)
│
└── gcc/
    ├── README.SUBSTRATE.md     # gcc-specific notes
    ├── fetch.sh                # download + sha256 + prereqs + patch
    ├── build.sh                # --stage=1 | --stage=2
    ├── series                  # ordered patch list
    ├── patches/                # 8 substrate patches
    ├── build/                  # extracted upstream tree (gitignored)
    ├── build-stage1/           # stage-1 build dir (gitignored)
    └── build-stage2/           # stage-2 build dir (gitignored)
```

Build artifacts go to `$STAGE1_PREFIX` (default `/opt/substrate`); the
substrate-ELF stage-2 binaries get staged into `$STAGE2_DESTDIR`
(default `dist-toolchain/`) for injection into `rootfs.img`.

## What gets built where

| | Stage 1 (cross) | Stage 2 (native-on-substrate) |
|---|---|---|
| build machine | Linux | Linux |
| host of resulting binaries | Linux | **Substrate** |
| target | Substrate | Substrate |
| install prefix | `$STAGE1_PREFIX` (`/opt/substrate`) | `/usr` (staged via `DESTDIR`) |
| install method | `sudo make install` | `make install DESTDIR=$STAGE2_DESTDIR`, then debugfs into rootfs |
| binary format | x86_64 ELF (Linux) | i386 ELF, OS/ABI Substrate |

Stage 1 gives you `/opt/substrate/bin/i386-unknown-substrate-{gcc,g++,as,ld,ar,nm,objdump,readelf,strip,...}` — the compiler you use **on Linux** to cross-compile substrate userland.

Stage 2 gives you binaries that are themselves substrate ELFs.  After staging, the rootfs's `/usr/bin/gcc` is a substrate-native compiler that runs *on substrate*.

## Dependencies and order

```
              ┌─────────────────────────────────────────────────┐
              │              host requirements                  │
              │  bash, perl, m4, make, gcc/g++, gmp/mpfr/mpc... │
              │  (gcc's fetch.sh pulls in-tree prereqs already) │
              └─────────────────────────────────────────────────┘
                                  │
                                  ▼
        ┌─────────────────────────────────────────────────┐
        │           1. binutils stage 1                   │
        │  (cross binutils on Linux → emits substrate ELF)│
        └─────────────────────────────────────────────────┘
                                  │
                                  ▼
        ┌─────────────────────────────────────────────────┐
        │           2. substrate libc + crt0 + libm       │
        │  cd lib/c && make ; cd lib/m && make            │
        │  (host-built but i386 ELF — staged into dist/)  │
        └─────────────────────────────────────────────────┘
                                  │
                                  ▼
        ┌─────────────────────────────────────────────────┐
        │  3. .so re-link with substrate-target ld        │
        │     libsys → libm → libc → libpthread/libdl     │
        │  (stamps ELFOSABI_SUBSTRATE on every input)     │
        └─────────────────────────────────────────────────┘
                                  │
                                  ▼
        ┌─────────────────────────────────────────────────┐
        │           4. gcc stage 1                        │
        │  (cross gcc on Linux + libgcc + crt*.o)         │
        │  ENABLE_LANGUAGES=c,c++ for stage-2 build later │
        └─────────────────────────────────────────────────┘
                                  │
                                  ▼
        ┌─────────────────────────────────────────────────┐
        │           5. binutils stage 2  (optional)       │
        │  Canadian cross — produces substrate-ELF tools  │
        └─────────────────────────────────────────────────┘
                                  │
                                  ▼
        ┌─────────────────────────────────────────────────┐
        │           6. gcc stage 2  (NOT YET FUNCTIONAL)  │
        │  Canadian cross — substrate-ELF compiler        │
        │  Blocked on libstdc++ (see below)               │
        └─────────────────────────────────────────────────┘
```

## Step-by-step

### 1. Provision the install prefix

```sh
sudo mkdir -p /opt/substrate
sudo chown $(id -u):$(id -g) /opt/substrate
```

### 2. Build binutils stage 1

```sh
cd contrib/binutils
./fetch.sh                  # downloads binutils-2.46.0.tar.xz, sha256, patches
./build.sh --stage=1        # ./build/ + ./build-stage1/, installs to /opt/substrate
```

Verify:
```sh
/opt/substrate/bin/i386-unknown-substrate-ld --version | head -1
/opt/substrate/bin/i386-unknown-substrate-as --version | head -1
```

### 3. Build substrate libc + libm + crt files

These are part of the substrate source tree, not contrib.  From the
repo root:

```sh
make -C lib/c                # libc.a, libc.so.0, crt0.o, crti.o, crtn.o
make -C lib/m                # libm.a, libm.so.0
make -C lib/sys              # libsys.so.0
make -C lib/pthread         # libpthread.so.0
make -C lib/dl               # libdl.so.0
```

### 4. Re-link the .so files with substrate-target ld

The host toolchain produced these with `OS/ABI: UNIX - System V`
(OSABI=0).  Walk the dependency chain re-linking each through
`i386-unknown-substrate-ld` so they carry `OS/ABI: Substrate`
(OSABI=64) end-to-end:

```sh
PATH=/opt/substrate/bin:$PATH
LD=/opt/substrate/bin/i386-unknown-substrate-ld

# libsys (no deps)
$LD -shared -m elf_i386_substrate -Bsymbolic-functions \
    -z max-page-size=0x1000 -z now -soname libsys.so.0 \
    --unresolved-symbols=ignore-all \
    -o lib/sys/libsys.so.0 lib/sys/*.pic.o

# libm (no substrate deps)
$LD -shared -m elf_i386_substrate -Bsymbolic-functions \
    -z max-page-size=0x1000 -z now -soname libm.so.0 \
    --unresolved-symbols=ignore-all \
    -o lib/m/libm.so.0 lib/m/src/*.pic.o

# libc (needs libm + libsys)
$LD -shared -m elf_i386_substrate -Bsymbolic-functions \
    -z max-page-size=0x1000 -z now -soname libc.so.0 \
    --unresolved-symbols=ignore-all \
    -Llib/m -l:libm.so.0 -Llib/sys -l:libsys.so.0 \
    -o lib/c/libc.so.0 $(find lib/c -name '*.pic.o' | sort)

# libpthread, libdl, libedit follow same pattern, with -l:libc.so.0
```

Sanity check:
```sh
for L in lib/sys/libsys lib/m/libm lib/c/libc; do
    /opt/substrate/bin/i386-unknown-substrate-readelf -h $L.so.0 | grep OS/ABI
done
# all three should print "OS/ABI:    Substrate"
```

### 5. Stage the sysroot

Stage 1 gcc needs to find substrate headers and libraries.  The
`--with-sysroot=$SUBSTRATE_TOP/dist` flag points it at `dist/`:

```sh
mkdir -p dist/usr/include dist/usr/lib dist/lib
cp -r include/* dist/usr/include/
cp sys/arch/i386/syscall.h dist/usr/include/arch/i386/   # if not staged
cp lib/c/libc.a lib/m/libm.a dist/usr/lib/
cp lib/c/crt0.o lib/c/crti.o lib/c/crtn.o dist/usr/lib/
cp lib/sys/libsys.so.0 lib/m/libm.so.0 lib/c/libc.so.0 dist/lib/
cp lib/pthread/libpthread.so.0 lib/dl/libdl.so.0 dist/lib/
ln -sf libc.so.0 dist/lib/libc.so
```

### 6. Build gcc stage 1

```sh
cd contrib/gcc
./fetch.sh                                     # downloads gcc-16.1.0
PATH=/opt/substrate/bin:$PATH \
    ENABLE_LANGUAGES=c,c++ ./build.sh --stage=1
```

`ENABLE_LANGUAGES=c,c++` is required if you plan to do stage 2 later — without `g++` in stage 1 the gcc stage-2 configure dies at the "C++14 features required" check.

Verify:
```sh
/opt/substrate/bin/i386-unknown-substrate-gcc --version | head -1
/opt/substrate/bin/i386-unknown-substrate-g++ --version | head -1
```

Compile a substrate-target hello world:
```sh
cat > /tmp/hi.c <<'EOF'
#include <stdio.h>
int main(void) { puts("hello from substrate gcc"); return 0; }
EOF
/opt/substrate/bin/i386-unknown-substrate-gcc \
    --sysroot=$(pwd)/dist -o /tmp/hello /tmp/hi.c
/opt/substrate/bin/i386-unknown-substrate-readelf -h /tmp/hello | grep OS/ABI
# → OS/ABI: Substrate
```

### 7. Update the rootfs image

```sh
for f in lib/sys/libsys.so.0 lib/m/libm.so.0 lib/c/libc.so.0 \
         lib/pthread/libpthread.so.0 lib/dl/libdl.so.0; do
    name=$(basename $f)
    debugfs -w -R "rm /lib/$name" rootfs.img
    debugfs -w -R "write $f /lib/$name" rootfs.img
done
for f in lib/c/crt0.o lib/c/crti.o lib/c/crtn.o lib/c/libc.a lib/m/libm.a; do
    name=$(basename $f)
    debugfs -w -R "rm /usr/lib/$name" rootfs.img 2>/dev/null
    debugfs -w -R "write $f /usr/lib/$name" rootfs.img
done
```

### 8. Stage 2 — currently blocked

`build.sh --stage=2` is wired up but `libstdc++` won't build cleanly yet.

**Status**: stage-2 gcc compile gets past every gateway *except*
libstdc++'s configure link tests.  Specifically:

- ✅ Stage 1 c+c++ produces working `g++` + `cc1plus`.
- ✅ `libstdc++` per-OS config directory exists (`os/substrate/`).
- ✅ `configure.host`, `crossconfig.m4`, `configure` all recognise
  `*-substrate*` (treated as glibc/Linux-shaped).
- ✅ Substrate libc provides `mbstowcs`, `wcstombs`, `mblen`, etc.
- ✅ `<sys/sem.h>`, `<xlocale.h>`, `<linux/types.h>`, `<linux/random.h>`
  stubs in substrate include tree.
- ✅ `restrict` → `__restrict` in libc public headers (C++ compat).
- ✅ `fenv_t` forward typedef in `math.h` for `<cmath>` path.
- ❌ libstdc++ configure attempts link tests after
  `GCC_NO_EXECUTABLES`; needs a properly-set-up cross sysroot the
  configure can actually link against.

**Path forward** (the next session's work):
1. Resolve `Link tests are not allowed after GCC_NO_EXECUTABLES` —
   either by making the substrate sysroot self-consistent enough
   that libstdc++'s configure link tests succeed, or by patching
   libstdc++ to skip the host-detection link tests on substrate.
2. Build `libstdc++.{a,so}` for substrate.
3. Re-run `build.sh --stage=2`.
4. `make install DESTDIR=$STAGE2_DESTDIR` populates
   `$STAGE2_DESTDIR/usr/{bin,lib,libexec,include,share}/...`.
5. Copy that tree into `rootfs.img` under the same paths.

## Reproducibility / re-vendoring

To bump binutils or gcc:

1. Edit `VERSION` and `SHA256` in the relevant `fetch.sh`.
2. `rm -rf build/{binutils,gcc}-*` to force re-extract.
3. `./fetch.sh` (or `./fetch.sh --no-network` if you've cached the tarball).
4. Rebase any patches that no longer apply against the new upstream.

Patches are deliberately small and stanza-local (each one
adds an entry to a list beside existing entries for FreeBSD,
Linux, etc.), so finding the right context after a bump is a
visual diff exercise rather than a deep merge.

## Known limits as of this writing

- **Stage 2 gcc is blocked on libstdc++** — see step 8.
- **Stage 2 binutils works in principle but is parked** — needs the
  same target sysroot used for gcc stage 2.
- **x86_64-substrate**: binutils accepts the triple; gcc has no
  `config.gcc` stanza for it.  Add one when substrate kernel grows
  x86_64 support.
- **DWARF unwinder**: `libgcc/config.host` registers no
  `md_unwind_header` — C++ exceptions across signal frames will not
  work.  Fix when we want them.
- **Self-hosted build (on-substrate)**: stage 2 is a Canadian cross
  from Linux.  Self-hosting needs bash/perl/m4/make/sed inside the
  substrate userland — separate effort.
