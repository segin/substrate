#!/bin/sh
#
# build.sh — end-to-end "whole shebang" build from a clean checkout.
#
# Order (each layer depends on the previous):
#   1.  contrib/build-toolchain.sh — stage-1 cross binutils+GCC
#       targeting i386-unknown-substrate, then a Canadian-cross
#       stage 2 that runs ON substrate.
#   2.  Native substrate components (kernel `sys/`, runtime libs
#       under `lib/`, the dynamic linker `sbin/ld.so/`, base
#       userland `bin/` + `sbin/`, helper libs under `usr.lib/`,
#       toolchain-helper utilities `usr.bin/`, manuals).
#   3.  contrib/ ports — each contrib/<pkg> with a fetch.sh +
#       build.sh.  Built in dependency order.  Output goes into
#       per-package ${SUBSTRATE_TOP}/dist-<pkg>/ staging trees so
#       partial rebuilds don't perturb each other.
#   4.  build-rootfs.sh — assembles dist/ from `sys/`, the libs,
#       every dist-*/ overlay, and bakes rootfs.img.
#
# Env knobs (all optional):
#
#   STAGE1_PREFIX     where the cross toolchain installs           default /opt/substrate
#   JOBS              -j parallelism                                default `nproc`
#   SKIP_TOOLCHAIN=1  reuse an existing $STAGE1_PREFIX, don't rebuild
#   SKIP_CONTRIB=1    skip every contrib/ port (just kernel + userland + image)
#   SKIP_IMAGE=1      stop after staging dist/, don't bake rootfs.img
#   ONLY="pkg1 pkg2"  build only these contrib ports (default: everything in
#                      the order baked into this script)
#
# A clean run from a fresh checkout:
#
#   git clone … substrate && cd substrate
#   sudo ./build.sh         # sudo: contrib/build-toolchain.sh writes to /opt/substrate
#
# Output:
#   ./rootfs.img   bootable ext2 image
#   ./dist/        staged root filesystem

set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
cd "$HERE"

: "${STAGE1_PREFIX:=/opt/substrate}"
: "${JOBS:=$(nproc 2>/dev/null || echo 4)}"
: "${SKIP_TOOLCHAIN:=0}"
: "${SKIP_CONTRIB:=0}"
: "${SKIP_IMAGE:=0}"
export STAGE1_PREFIX JOBS SUBSTRATE_TOP="$HERE"

# contrib build order.  Each name is a directory under contrib/.
# Dependencies:
#   bzip2          → libarchive (--with-bz2lib)
#   zlib via libz  → curl, libarchive (but we don't ship a zlib port yet —
#                    those packages disable it or use the host-bundled copy)
#   libiconv       → curl, mpg123, zsh (DT_NEEDED libiconv.so.2)
#   openssl        → curl (--with-openssl)
#   ncurses        → (substrate ships a stub libcurses in lib/curses; no
#                    standalone port yet)
#   make           → independent
#   tzdata         → independent (data files only)
#   bash/zsh       → independent (zsh DT_NEEDED libiconv, libcurses)
#   gzip/inetutils/mpg123/openssl/curl/libarchive/sed → independent of each other
#   expr           → independent
DEFAULT_CONTRIB="bzip2 libiconv openssl gzip tzdata make sed expr libarchive inetutils mpg123 curl zsh"
: "${ONLY:=${DEFAULT_CONTRIB}}"

step() { printf '\n=========================  %s  =========================\n' "$*"; }
note() { printf '   %s\n' "$*"; }

#-----------------------------------------------------------------------
# Stage 0: toolchain
#-----------------------------------------------------------------------
if [ "$SKIP_TOOLCHAIN" = 1 ]; then
    step "Stage 0: TOOLCHAIN (skipped — SKIP_TOOLCHAIN=1)"
    note "Reusing the existing cross toolchain at $STAGE1_PREFIX."
else
    step "Stage 0: TOOLCHAIN (contrib/build-toolchain.sh)"
    note "binutils 2.46.0 + GCC 16.1.0, stage 1 cross + stage 2 Canadian cross"
    note "installs into $STAGE1_PREFIX (stage 1) and /tmp/gcc-stage2-staging (stage 2)"
    contrib/build-toolchain.sh
fi

export PATH="${STAGE1_PREFIX}/bin:${PATH}"

#-----------------------------------------------------------------------
# Stage 1: native substrate components
#-----------------------------------------------------------------------
step "Stage 1a: kernel (sys/)"
make -C sys -j"$JOBS"

step "Stage 1b: substrate runtime libraries (lib/, usr.lib/)"
make -C lib
make -C usr.lib

step "Stage 1c: dynamic linker (sbin/ld.so/)"
make -C sbin/ld.so

step "Stage 1d: base userland (bin/, sbin/)"
make -C bin
make -C sbin

step "Stage 1e: toolchain-helper utilities (usr.bin/)"
make -C usr.bin

#-----------------------------------------------------------------------
# Stage 2: contrib ports
#-----------------------------------------------------------------------
if [ "$SKIP_CONTRIB" = 1 ]; then
    step "Stage 2: CONTRIB (skipped — SKIP_CONTRIB=1)"
else
    for pkg in $ONLY; do
        [ -d "contrib/$pkg" ] || { echo "build.sh: no such contrib/$pkg" >&2; exit 1; }
        [ -x "contrib/$pkg/fetch.sh" ] || { echo "build.sh: contrib/$pkg has no fetch.sh" >&2; exit 1; }
        [ -x "contrib/$pkg/build.sh" ] || { echo "build.sh: contrib/$pkg has no build.sh" >&2; exit 1; }

        step "Stage 2: contrib/$pkg"
        ( cd "contrib/$pkg" && ./fetch.sh )
        ( cd "contrib/$pkg" && ./build.sh )
    done
fi

#-----------------------------------------------------------------------
# Stage 3: image
#-----------------------------------------------------------------------
step "Stage 3: dist/ staging"
./build-rootfs.sh --dist
./build-rootfs.sh --toolchain

if [ "$SKIP_IMAGE" = 1 ]; then
    note "Skipping image bake (SKIP_IMAGE=1).  dist/ is ready under $HERE/dist/."
else
    step "Stage 3b: rootfs.img"
    ./build-rootfs.sh --image
    note "Image at $HERE/rootfs.img"
fi

step "DONE"
note "Run: ./run.sh   to boot rootfs.img under qemu."
