#!/bin/bash
#
# build64.sh — the 64-bit counterpart of build.sh: from a clean checkout to
# rootfs64.img, an image whose kernel and userland are both 64-bit.
#
#   1.  contrib/build-toolchain64.sh — substrate's 64-bit libraries, the
#       x86_64-unknown-substrate cross binutils and GCC, their runtime
#       (libgcc_s.so.1, libstdc++.so.6) and the toolchain checks.
#   2.  programs built with that toolchain, staged for the image under
#       dist-overlay64/: the boot test's init, the toolchain test
#       programs, and the contrib ports that build for the 64-bit target
#       (contrib/port64.sh; the list is DEFAULT_CONTRIB64 below).
#   3.  ./build-rootfs.sh --arch=x86_64 --dist --image — the 64-bit kernel,
#       the in-tree userland built 64-bit, both architectures' libraries,
#       and the image.
#   4.  tests/rootfs64/boot-test.sh — boot the image and run the smoke
#       test on it.
#
# Env:
#   STAGE1_PREFIX     where the cross toolchain installs   (default /opt/substrate)
#   SKIP_TOOLCHAIN=1  reuse an installed toolchain: build-toolchain64.sh
#                     then only refreshes the libraries, sysroot and checks
#   SKIP_BOOT_TEST=1  stop after baking the image
#   IMAGE_SIZE_MIB    image size                            (default 4096,
#                     build-rootfs.sh's own; the file is sparse)
#
# Usage:
#   ./build64.sh

set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
cd "$HERE"

: "${STAGE1_PREFIX:=/opt/substrate}"
: "${SKIP_TOOLCHAIN:=0}"
: "${SKIP_BOOT_TEST:=0}"
export STAGE1_PREFIX

step() { echo ""; echo "=========================  $*  ========================="; }

step "Stage 1: 64-bit cross toolchain"
SKIP_BUILD="$SKIP_TOOLCHAIN" contrib/build-toolchain64.sh

step "Stage 2: programs built with the cross toolchain"
T64="$STAGE1_PREFIX/bin/x86_64-unknown-substrate"
STAGE="$HERE/dist-overlay64/dist-rootfs64-tests"
rm -rf "$HERE/dist-overlay64/dist-rootfs64-tests"
mkdir -p "$STAGE/usr/libexec/rootfs64"
"$T64-gcc" -O2 -Wall -Wextra -Werror \
    -o "$STAGE/usr/libexec/rootfs64/smokeinit" tests/rootfs64/smokeinit.c
install -m 755 tests/rootfs64/smoke.sh "$STAGE/usr/libexec/rootfs64/smoke.sh"
# The toolchain's own test programs, as tests/toolchain64/check.sh left
# them: run on the image by smoke.sh.
for p in hello cxx; do
    install -m 755 "tests/toolchain64/out/$p" "$STAGE/usr/libexec/rootfs64/$p"
done

step "Stage 2b: contrib ports for the 64-bit target"
# contrib/port64.sh builds a port with the 64-bit toolchain from the port's
# own scripts and stages it under dist-overlay64/dist-<port>, which
# build-rootfs.sh --arch=x86_64 overlays.  The order is build.sh's
# DEFAULT_CONTRIB order -- each port may need the ones before it -- and
# the list is the prefix of it that is known to build for 64-bit.
# ONLY64="a b c" overrides it; ONLY64="" builds none.
DEFAULT_CONTRIB64="bzip2 libiconv zlib openssl ncurses gzip tzdata make sed m4 flex autoconf automake libtool expr libarchive mpg123 curl nginx inetutils zsh e2fsprogs e2tools gmp mpfr"
: "${ONLY64=${DEFAULT_CONTRIB64}}"
for pkg in $ONLY64; do
    step "Stage 2b: contrib/$pkg (64-bit)"
    contrib/port64.sh "$pkg"
done

step "Stage 3: 64-bit userland and rootfs64.img"
./build-rootfs.sh --arch=x86_64 --dist --image --no-boot

if [ "$SKIP_BOOT_TEST" = 1 ]; then
    step "Stage 4: boot test (skipped — SKIP_BOOT_TEST=1)"
else
    step "Stage 4: boot rootfs64.img and run the smoke test"
    tests/rootfs64/boot-test.sh
fi

echo ""
echo "==> build64.sh done: $HERE/rootfs64.img"
