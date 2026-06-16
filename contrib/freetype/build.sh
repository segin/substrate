#!/bin/sh
# contrib/freetype/build.sh — cross-compile FreeType 2.13.2 for substrate.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
VERSION="2.13.2"; TREE_DIR="${HERE}/build/freetype-${VERSION}"
if [ -z "${SUBSTRATE_TOP:-}" ]; then
    p="${HERE}"; while [ "${p}" != "/" ] && [ ! -f "${p}/AGENTS.md" ] && [ ! -f "${p}/CLAUDE.md" ]; do p=$(dirname "${p}"); done
    SUBSTRATE_TOP="${p}"
fi
: "${STAGE1_PREFIX:=/opt/substrate}"; : "${DESTDIR:=${SUBSTRATE_TOP}/dist-overlay/dist-freetype}"
: "${JOBS:=$(nproc 2>/dev/null || echo 4)}"
PATH="${STAGE1_PREFIX}/bin:${PATH}"; export PATH
. "${HERE}/../substrate-autotools.sh"
[ -d "${TREE_DIR}" ] || { echo "build.sh: run ./fetch.sh first" >&2; exit 1; }
ZL="${SUBSTRATE_TOP}/dist-overlay/dist-zlib"; PNG="${SUBSTRATE_TOP}/dist-overlay/dist-libpng"
for d in "${ZL}" "${PNG}"; do [ -d "${d}/usr" ] || { echo "build.sh: ${d} missing" >&2; exit 1; }; done

export CPPFLAGS="-I${ZL}/usr/include -I${PNG}/usr/include"
export LDFLAGS="-L${ZL}/usr/lib -L${PNG}/usr/lib -Wl,-rpath-link,${ZL}/usr/lib -Wl,-rpath-link,${PNG}/usr/lib -Wl,--copy-dt-needed-entries"
export PKG_CONFIG_LIBDIR="${ZL}/usr/lib/pkgconfig:${PNG}/usr/lib/pkgconfig"

substrate_libtool_fix "${TREE_DIR}/builds/unix/configure"
# FreeType drives builds/unix/configure from the top; run it in-tree.
cd "${TREE_DIR}"
make distclean >/dev/null 2>&1 || true
./configure \
    --host=i386-unknown-substrate \
    --prefix=/usr --libdir=/usr/lib --includedir=/usr/include \
    --enable-shared --enable-static \
    --with-zlib --with-png --without-harfbuzz --without-brotli --without-bzip2 \
    CC=i386-unknown-substrate-gcc \
    AR=i386-unknown-substrate-ar RANLIB=i386-unknown-substrate-ranlib \
    CC_BUILD=gcc \
    CFLAGS="-march=i486 -mtune=i486 -O2 -g"
make -j"${JOBS}"
rm -rf "${DESTDIR}"; mkdir -p "${DESTDIR}"
make install DESTDIR="${DESTDIR}"
substrate_so_finalize "${DESTDIR}"
echo "==> freetype staged at ${DESTDIR}"
