#!/bin/sh
# contrib/libxkbfile/build.sh — cross-build libxkbfile for substrate.
# Produces /usr/lib/libxkbfile.{a,so.1} + headers + pkgconfig.
#
# libxkbfile is a shared-memory fence primitive used by X clients
# and the X server for cross-process synchronization (DRI3 sync,
# present extension, etc).  Required dependency of xorg-server.

set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
VERSION="1.1.3"
TREE_DIR="${HERE}/build/libxkbfile-${VERSION}"
BUILD_DIR="${HERE}/build/build-stage-substrate"

if [ -z "${SUBSTRATE_TOP:-}" ]; then
    p="${HERE}"
    while [ "${p}" != "/" ] && [ ! -f "${p}/AGENTS.md" ] && [ ! -f "${p}/CLAUDE.md" ]; do
        p=$(dirname "${p}")
    done
    SUBSTRATE_TOP="${p}"
fi
: "${STAGE1_PREFIX:=/opt/substrate}"
: "${XORGPROTO_STAGE:=${SUBSTRATE_TOP}/dist-overlay/dist-xorgproto}"
: "${LIBX11_STAGE:=${SUBSTRATE_TOP}/dist-overlay/dist-libX11}"
: "${DESTDIR:=${SUBSTRATE_TOP}/dist-overlay/dist-libxkbfile}"
: "${JOBS:=$(nproc 2>/dev/null || echo 4)}"

PATH="${STAGE1_PREFIX}/bin:${PATH}"
export PATH

[ -d "${TREE_DIR}" ] || { echo "build.sh: run ./fetch.sh first" >&2; exit 1; }

rm -rf "${BUILD_DIR}"
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

# Resolve dependencies from the cross sysroot, where every port before this
# one has been installed (build.sh syncs after each).  libX11 is built on
# XCB, so x11.pc requires xcb.pc, which requires xau.pc and pthread-stubs.pc;
# the sysroot has the whole set, headers included.
#
# LIBDIR rather than PATH: PATH only ADDS to pkg-config's defaults, which do
# not include the sysroot, so a PATH listing a couple of dist trees resolved
# the rest out of the build host's /usr/lib/pkgconfig -- feeding host flags
# into a cross build wherever the host happened to have X installed, and
# failing outright where it did not.
export PKG_CONFIG_LIBDIR="${STAGE1_PREFIX}/i386-unknown-substrate/lib/pkgconfig"
export LDFLAGS="-fno-pie -Wl,--copy-dt-needed-entries"

echo "==> configure"
"${TREE_DIR}/configure" \
    --host=i386-unknown-substrate \
    --prefix=/usr \
    --libdir=/usr/lib \
    --enable-shared \
    --enable-static \
    CC=i386-unknown-substrate-gcc \
    AR=i386-unknown-substrate-ar \
    RANLIB=i386-unknown-substrate-ranlib \
    CFLAGS="-march=i486 -mtune=i486 -O2 -g -fno-pie"

echo "==> make -j${JOBS}"
make -j"${JOBS}"

echo "==> install into ${DESTDIR}"
rm -rf "${DESTDIR}"
mkdir -p "${DESTDIR}"
make install DESTDIR="${DESTDIR}"

rm -f "${DESTDIR}"/usr/lib/*.la

for so in "${DESTDIR}"/usr/lib/*.so.*; do
    [ -f "${so}" ] || continue
    [ -L "${so}" ] && continue
    printf '\100' | dd of="${so}" bs=1 seek=7 count=1 conv=notrunc status=none
    echo "  OSABI->substrate on $(basename "${so}")"
done

echo "==> Done.  libxkbfile staged under ${DESTDIR}"
