#!/bin/bash
#
# build64.sh — the 64-bit counterpart of build.sh: from a clean checkout to
# rootfs64.img, an image whose kernel and userland are both 64-bit.
#
#   1.  contrib/build-toolchain64.sh — substrate's 64-bit libraries, the
#       x86_64-unknown-substrate cross binutils and GCC, their runtime
#       (libgcc_s.so.1, libstdc++.so.6) and the toolchain checks.
#   2.  programs built with that toolchain, staged for the image under
#       dist-overlay64/: the boot test's init and the toolchain test
#       programs.  (Contrib ports for the 64-bit target would be staged
#       the same way; none has been ported yet.)
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
