#!/bin/sh
# contrib/harfbuzz/build.sh — cross-compile HarfBuzz 2.6.8 for substrate.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
VERSION="2.6.8"; TREE_DIR="${HERE}/build/harfbuzz-${VERSION}"; BUILD_DIR="${HERE}/build/build-substrate"
if [ -z "${SUBSTRATE_TOP:-}" ]; then
    p="${HERE}"; while [ "${p}" != "/" ] && [ ! -f "${p}/AGENTS.md" ] && [ ! -f "${p}/CLAUDE.md" ]; do p=$(dirname "${p}"); done
    SUBSTRATE_TOP="${p}"
fi
: "${STAGE1_PREFIX:=/opt/substrate}"; : "${DESTDIR:=${SUBSTRATE_TOP}/dist-harfbuzz}"
: "${JOBS:=$(nproc 2>/dev/null || echo 4)}"
PATH="${STAGE1_PREFIX}/bin:${PATH}"; export PATH
. "${HERE}/../substrate-autotools.sh"
[ -d "${TREE_DIR}" ] || { echo "build.sh: run ./fetch.sh first" >&2; exit 1; }

substrate_sysroot "${HERE}/build/sysroot" glib2 libffi zlib freetype libpng
export CXXFLAGS="-march=i486 -mtune=i486 -O2 -g -std=gnu++14"
export CFLAGS="-march=i486 -mtune=i486 -O2 -g"

substrate_libtool_fix "${TREE_DIR}/configure"
rm -rf "${BUILD_DIR}"; mkdir -p "${BUILD_DIR}"; cd "${BUILD_DIR}"
"${TREE_DIR}/configure" \
    --host=i386-unknown-substrate \
    --prefix=/usr --libdir=/usr/lib --includedir=/usr/include \
    --enable-shared --enable-static \
    --with-glib=yes --with-freetype=yes --with-cairo=no --with-icu=no \
    --without-gobject --disable-introspection \
    CC=i386-unknown-substrate-gcc CXX=i386-unknown-substrate-g++ \
    AR=i386-unknown-substrate-ar RANLIB=i386-unknown-substrate-ranlib \
    CC_FOR_BUILD=gcc CXX_FOR_BUILD=g++
make -j"${JOBS}"
rm -rf "${DESTDIR}"; mkdir -p "${DESTDIR}"
make install DESTDIR="${DESTDIR}"
substrate_so_finalize "${DESTDIR}"
echo "==> harfbuzz staged at ${DESTDIR}"
