# GCC — Substrate patch series

This directory carries the substrate-target port of GCC as a patch
series against an unmodified upstream release.  Nothing in this
directory is a vendored source file.  The full upstream tree is
fetched on demand by `./fetch.sh`, which then applies the patches
listed in `series`.

| | |
|---|---|
| Upstream version | **16.1.0** |
| Tarball SHA-256  | `50efb4d94c3397aff3b0d61a5abd748b4dd31d9d3f2ab7be05b171d36a510f79` |
| Source           | `https://ftp.gnu.org/gnu/gcc/gcc-16.1.0/gcc-16.1.0.tar.xz` |
| Target triple    | `i386-unknown-substrate` (x86_64 deferred — kernel is i386 only) |
| Languages        | C only (C++/Fortran/Go/D/Ada/M2 all `--disable`d) |

## Quick start

```sh
# Prerequisites: contrib/binutils stage 1 already installed at
# $STAGE1_PREFIX/bin (default /opt/substrate-toolchain).

cd contrib/gcc
./fetch.sh                              # download + sha256 + prereqs + patch
./build.sh --stage=1                    # cross gcc on Linux → substrate
```

The fetch script invokes `contrib/download_prerequisites` from the
upstream tree, which pulls gmp / mpfr / mpc / isl into the gcc source
directory so the GCC build is self-contained.

## Two build stages

`build.sh --stage=1`
:   **Cross-compiler.** build = host = Linux; target = substrate.
    Produces `i386-unknown-substrate-{gcc,cpp,g++,...}` running on
    Linux, emitting substrate ELFs.  Needs binutils stage 1 already
    installed.

`build.sh --stage=2`
:   **Native-on-substrate (Canadian cross).** build = Linux; host =
    target = substrate; `--prefix=/usr`.  Uses stage-1 gcc to compile
    gcc itself into substrate ELFs.  Stages into `$STAGE2_DESTDIR`
    for dropping into `rootfs.img`.  Requires stage 1 of both gcc
    and binutils.

Both stages are orchestrated by `../build-toolchain.sh`.

## What the patches do

| # | Touches | Purpose |
|---|---|---|
| 0001 | `config.sub` | accept `substrate*` as an OS suffix |
| 0002 | `gcc/config.gcc` | new `i[34567]86-*-substrate*` target stanza |
| 0003 | `gcc/config/substrate.h` (new) | common OS header — cpp builtins (`__substrate__`, `__unix__`, `system=posix/substrate` asserts), `LIB_SPEC` linking via `-l:libc.so.0` |
| 0004 | `gcc/config/i386/substrate.h` (new) | i386-specific specs — `GLIBC_DYNAMIC_LINKER = /sbin/ld.so`, `LINK_EMULATION = elf_i386_substrate`, startfile spec pulling `crt0.o` + `crti.o`/`crtn.o` + `crtbegin*.o`/`crtend*.o` |
| 0005 | `libgcc/config.host` | register `i386-substrate` for libgcc: `t-crtstuff` + `t-dfprules` (decimal FP rules) |

## What's deliberately deferred

- **x86_64-substrate**.  Kernel is i386 only today.  Binutils accepts
  the triple but gcc/config.gcc has no stanza for it.  Adding the
  stanza later is a 5-line patch beside the freebsd64 / linux64
  models.
- **libstdc++ and friends.**  `--disable-libstdcxx --disable-libgomp
  --disable-libitm --disable-libsanitizer --disable-libquadmath
  --disable-libvtv --disable-libssp` — every C++/runtime library is
  off.  C is enough to bring the system up to self-hosting.
- **DWARF unwinder / signal-frame backtrace.**  No `md_unwind_header`
  is registered.  C++ exceptions won't roll out of signal handlers
  correctly without one; revisit when libstdc++ is wanted.
- **Self-hosted build (stage 2).**  Wired up but parked until the
  substrate sysroot (`dist/`) carries enough headers / libraries
  that libgcc can actually compile against it.

## Smoke test (after `build.sh --stage=1`)

```sh
PATH=/opt/substrate-toolchain/bin:$PATH

cat > /tmp/hi.c <<'EOF'
int main(void) { return 42; }
EOF

# -nostdlib until substrate's crt0.o is staged in the sysroot.
i386-unknown-substrate-gcc -nostdlib -static -o /tmp/hi /tmp/hi.c

file /tmp/hi
# → ELF 32-bit LSB executable, Intel i386, ..., statically linked

i386-unknown-substrate-readelf -h /tmp/hi | grep OS/ABI
# → OS/ABI: Substrate

i386-unknown-substrate-objdump -p /tmp/hi | head -1
# → file format elf32-i386-substrate
```

## Re-vendoring upstream

Bumping to a newer GCC release:

1. Edit `VERSION` and `SHA256` in `fetch.sh`.
2. Remove the stale tree: `rm -rf build/gcc-*/`
3. Re-run `./fetch.sh` — at least patch 0002 (config.gcc) is likely
   to need rebasing.  GCC reorganises the per-target stanza file
   between major releases; the substrate stanza belongs beside the
   `dragonfly` / `freebsd` / `netbsd` family in every version so
   far.
4. Run `./build.sh --stage=1` to verify.
