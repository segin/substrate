#!/bin/sh
# contrib/libpng/build.sh — cross-compile libpng 1.6.43 for substrate.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
VERSION="1.6.43"
TREE_DIR="${HERE}/build/libpng-${VERSION}"
BUILD_DIR="${HERE}/build/build-substrate"
if [ -z "${SUBSTRATE_TOP:-}" ]; then
    p="${HERE}"
    while [ "${p}" != "/" ] && [ ! -f "${p}/AGENTS.md" ] && [ ! -f "${p}/CLAUDE.md" ]; do p=$(dirname "${p}"); done
    SUBSTRATE_TOP="${p}"
fi
: "${STAGE1_PREFIX:=/opt/substrate}"
: "${DESTDIR:=${SUBSTRATE_TOP}/dist-libpng}"
: "${JOBS:=$(nproc 2>/dev/null || echo 4)}"
PATH="${STAGE1_PREFIX}/bin:${PATH}"; export PATH
. "${HERE}/../substrate-autotools.sh"
[ -d "${TREE_DIR}" ] || { echo "build.sh: run ./fetch.sh first" >&2; exit 1; }
ZL="${SUBSTRATE_TOP}/dist-zlib"
[ -d "${ZL}/usr" ] || { echo "build.sh: dist-zlib missing — build contrib/zlib first" >&2; exit 1; }

export CPPFLAGS="-I${ZL}/usr/include"
export LDFLAGS="-L${ZL}/usr/lib -Wl,-rpath-link,${ZL}/usr/lib -Wl,--copy-dt-needed-entries"
export PKG_CONFIG_LIBDIR="${ZL}/usr/lib/pkgconfig"

substrate_libtool_fix "${TREE_DIR}/configure"

rm -rf "${BUILD_DIR}"; mkdir -p "${BUILD_DIR}"; cd "${BUILD_DIR}"
"${TREE_DIR}/configure" \
    --host=i386-unknown-substrate \
    --prefix=/usr --libdir=/usr/lib --includedir=/usr/include \
    --enable-shared --enable-static \
    --disable-tools \
    CC=i386-unknown-substrate-gcc \
    AR=i386-unknown-substrate-ar RANLIB=i386-unknown-substrate-ranlib \
    CC_FOR_BUILD=gcc \
    CFLAGS="-march=i486 -mtune=i486 -O2 -g"
make -j"${JOBS}"
rm -rf "${DESTDIR}"; mkdir -p "${DESTDIR}"
make install DESTDIR="${DESTDIR}"
substrate_so_finalize "${DESTDIR}"
echo "==> libpng staged at ${DESTDIR}"
