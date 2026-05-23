#!/bin/sh
# contrib/libxshmfence/build.sh — cross-build libxshmfence for substrate.
# Produces /usr/lib/libxshmfence.{a,so.1} + headers + pkgconfig.
#
# libxshmfence is a shared-memory fence primitive used by X clients
# and the X server for cross-process synchronization (DRI3 sync,
# present extension, etc).  Required dependency of xorg-server.

set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
VERSION="1.3.2"
TREE_DIR="${HERE}/build/libxshmfence-${VERSION}"
BUILD_DIR="${HERE}/build/build-stage-substrate"

if [ -z "${SUBSTRATE_TOP:-}" ]; then
    p="${HERE}"
    while [ "${p}" != "/" ] && [ ! -f "${p}/AGENTS.md" ] && [ ! -f "${p}/CLAUDE.md" ]; do
        p=$(dirname "${p}")
    done
    SUBSTRATE_TOP="${p}"
fi
: "${STAGE1_PREFIX:=/opt/substrate}"
: "${XORGPROTO_STAGE:=${SUBSTRATE_TOP}/dist-xorgproto}"
: "${DESTDIR:=${SUBSTRATE_TOP}/dist-libxshmfence}"
: "${JOBS:=$(nproc 2>/dev/null || echo 4)}"

PATH="${STAGE1_PREFIX}/bin:${PATH}"
export PATH

[ -d "${TREE_DIR}" ] || { echo "build.sh: run ./fetch.sh first" >&2; exit 1; }

rm -rf "${BUILD_DIR}"
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

export PKG_CONFIG_PATH="${XORGPROTO_STAGE}/usr/lib/pkgconfig"
export CPPFLAGS="-I${XORGPROTO_STAGE}/usr/include"

echo "==> configure"
"${TREE_DIR}/configure" \
    --host=i386-unknown-substrate \
    --prefix=/usr \
    --libdir=/usr/lib \
    --enable-shared \
    --enable-static \
    --with-shared-memory-dir=/dev/shm \
    CC=i386-unknown-substrate-gcc \
    AR=i386-unknown-substrate-ar \
    RANLIB=i386-unknown-substrate-ranlib \
    CFLAGS="-march=i486 -mtune=i486 -O2 -g -fno-pie" \
    LDFLAGS="-fno-pie"

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

echo "==> Done.  libxshmfence staged under ${DESTDIR}"
