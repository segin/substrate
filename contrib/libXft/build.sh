#!/bin/sh
# contrib/libXft/build.sh — cross-compile libXft for substrate.
# Deps (fontconfig, freetype2, xrender, x11) are already in the cross
# sysroot with their .pc files, so we point pkg-config there.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
SUBSTRATE_TOP="$(cd "$HERE/../.." && pwd)"
LIB="libXft"; VERSION="2.3.9"
: "${STAGE1_PREFIX:=/opt/substrate}"
: "${DESTDIR:=${SUBSTRATE_TOP}/dist-overlay/dist-${LIB}}"
: "${JOBS:=$(nproc 2>/dev/null || echo 4)}"
PATH="${STAGE1_PREFIX}/bin:${PATH}"; export PATH
. "${HERE}/../substrate-autotools.sh"
SR="${STAGE1_PREFIX}/i386-unknown-substrate"
TREE_DIR="${HERE}/build/libXft-${VERSION}"
BUILD_DIR="${HERE}/build/obj"
[ -d "${TREE_DIR}" ] || { echo "build.sh: run ./fetch.sh first" >&2; exit 1; }

export PKG_CONFIG_LIBDIR="${SR}/lib/pkgconfig:${SUBSTRATE_TOP}/contrib/libxcb/pkgconfig"
export CPPFLAGS="-I${SR}/include -I${SR}/include/freetype2"
export LDFLAGS="-L${SR}/lib -Wl,-rpath-link,${SR}/lib -Wl,--copy-dt-needed-entries"

rm -rf "${BUILD_DIR}"; mkdir -p "${BUILD_DIR}"; cd "${BUILD_DIR}"
substrate_libtool_fix "${TREE_DIR}/configure"
echo "==> configure"
"${TREE_DIR}/configure" \
    --host=i386-unknown-substrate \
    --prefix=/usr --libdir=/usr/lib --includedir=/usr/include \
    --enable-shared --enable-static --disable-docs \
    CC=i386-unknown-substrate-gcc \
    AR=i386-unknown-substrate-ar \
    RANLIB=i386-unknown-substrate-ranlib \
    CC_FOR_BUILD=gcc \
    CFLAGS="-march=i486 -mtune=i486 -O2 -g -fno-pie"

echo "==> make -j${JOBS}"; make -j"${JOBS}"
echo "==> install into ${DESTDIR}"
rm -rf "${DESTDIR}"; mkdir -p "${DESTDIR}"; make install DESTDIR="${DESTDIR}"
rm -f "${DESTDIR}"/usr/lib/*.la
# Stamp ELFOSABI_SUBSTRATE on the produced shared objects.
_n=0
for so in "${DESTDIR}"/usr/lib/*.so.*; do
    [ -f "${so}" ] || continue; [ -L "${so}" ] && continue
    printf '\100' | dd of="${so}" bs=1 seek=7 count=1 conv=notrunc status=none; _n=$((_n+1))
done
echo "  OSABI->substrate on ${_n} shared objects"
echo "==> Done.  ${LIB} staged under ${DESTDIR}"
