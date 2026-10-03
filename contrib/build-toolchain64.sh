#!/bin/sh
#
# build-toolchain64.sh — build the 64-bit cross toolchain,
# x86_64-unknown-substrate: binutils and GCC (C and C++) that run on the
# Linux build host and produce native 64-bit substrate programs (LP64,
# dynamic linker /sbin/ld64.so, libraries in /lib64 and /usr/lib64 — see
# docs/specs/abi-amd64.md).
#
# It is stage 1 only: there is no 64-bit compiler that runs ON substrate.
# It installs beside the 32-bit toolchain in $STAGE1_PREFIX; every tool is
# prefixed with its triple and each target has its own sysroot directory,
# so neither disturbs the other.
#
# The order matters, and is the same dance the 32-bit toolchain does in
# build.sh, in one place:
#
#   1. substrate's 64-bit libraries (lib/, usr.lib/, ARCH=x86_64).  These
#      are built by the HOST compiler, as the whole in-tree userland is, so
#      unlike the contrib ports they do not wait for the cross compiler.
#   2. binutils.
#   3. gcc: cc1, the driver, and the static libgcc.
#   4. mirror the libraries, crt objects and headers into the sysroot.
#   5. the target runtime: the shared libgcc_s.so.1 and libstdc++, which
#      link against the libraries of step 1.
#   6. the specs override and the `libX.so` linker names
#      (contrib/gcc/install-specs.sh).
#   7. tests/toolchain64/check.sh: compile and link C and C++ programs
#      and check what came out.
#
# Env:
#   STAGE1_PREFIX   install prefix                 (default /opt/substrate)
#   PARALLEL        make -j                        (default $(nproc))
#   SKIP_BUILD=1    skip steps 2, 3 and 5: reuse the installed toolchain
#                   and only refresh the libraries, sysroot and specs
#
# Usage:
#   contrib/build-toolchain64.sh

set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
SUBSTRATE_TOP="$(cd "$HERE/.." && pwd)"
export SUBSTRATE_TOP
export STAGE1_PREFIX="${STAGE1_PREFIX:-/opt/substrate}"
export PARALLEL="${PARALLEL:-$(nproc 2>/dev/null || echo 4)}"
export TARGET_TRIPLE=x86_64-unknown-substrate
export PATH="${STAGE1_PREFIX}/bin:${PATH}"

step() { echo ""; echo "==> [toolchain64] $*"; }

step "1/7 substrate's 64-bit libraries (host compiler)"
make -C "$SUBSTRATE_TOP/lib" ARCH=x86_64 -j "$PARALLEL"
make -C "$SUBSTRATE_TOP/usr.lib" ARCH=x86_64 -j "$PARALLEL"

if [ "${SKIP_BUILD:-0}" != 1 ]; then
    for c in binutils gcc; do
        if ! ls -d "$HERE/$c/build/$c"-*/ >/dev/null 2>&1; then
            step "fetching $c"
            (cd "$HERE/$c" && ./fetch.sh)
        fi
    done

    step "2/7 binutils"
    (cd "$HERE/binutils" && ./build.sh --stage=1)

    # Before gcc, not only after it: step 3 already compiles libgcc, and
    # libgcc's unwinder includes <link.h> for struct dl_phdr_info.  The
    # in-tree compiler looks in this sysroot ahead of --with-sysroot's
    # dist/usr/include, which on a developer's machine can be a stale
    # staging of the 32-bit headers -- a libgcc_s.so.1 built against that
    # walks the program headers with the wrong layout and aborts on the
    # first throw.
    step "sysroot (headers and libraries for the libgcc build)"
    "$SUBSTRATE_TOP/scripts/sync-sysroot.sh" --x86_64

    step "3/7 gcc (compiler and libgcc)"
    (cd "$HERE/gcc" && ./build.sh --stage=1)
else
    step "2/7, 3/7 skipped (SKIP_BUILD=1): reusing $STAGE1_PREFIX"
fi

step "4/7 sysroot"
"$SUBSTRATE_TOP/scripts/sync-sysroot.sh" --x86_64

step "installing the specs override"
"$HERE/gcc/install-specs.sh"

if [ "${SKIP_BUILD:-0}" != 1 ]; then
    step "5/7 target runtime (libgcc_s.so.1, libstdc++)"
    (cd "$HERE/gcc" && ./build.sh --target-runtime)
fi

step "6/7 sysroot again, and the linker names"
"$SUBSTRATE_TOP/scripts/sync-sysroot.sh" --x86_64
"$HERE/gcc/install-specs.sh"

step "7/7 tests/toolchain64/check.sh"
"$SUBSTRATE_TOP/tests/toolchain64/check.sh"

echo ""
echo "==> [toolchain64] done: ${STAGE1_PREFIX}/bin/${TARGET_TRIPLE}-gcc"
