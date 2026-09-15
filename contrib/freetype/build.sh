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

# FREETYPE_WITH_HARFBUZZ=1 is the second pass, run by contrib/freetype-harfbuzz
# after harfbuzz is staged.  The two libraries depend on each other -- harfbuzz
# is built --with-freetype, and FreeType's autohinter uses HarfBuzz to cover
# glyphs no character maps to -- so the first pass must be --without-harfbuzz.
#
# The flags name the harfbuzz staging tree directly rather than asking
# pkg-config: harfbuzz.pc lists freetype2 under Requires.private, so resolving
# it would put the FIRST pass's freetype2 headers on FreeType's own include
# path.  configure takes a preset HARFBUZZ_CFLAGS/HARFBUZZ_LIBS as-is.
if [ "${FREETYPE_WITH_HARFBUZZ:-0}" = 1 ]; then
    HB="${SUBSTRATE_TOP}/dist-overlay/dist-harfbuzz"; GLIB="${SUBSTRATE_TOP}/dist-overlay/dist-glib2"
    for d in "${HB}" "${GLIB}"; do [ -d "${d}/usr" ] || { echo "build.sh: ${d} missing" >&2; exit 1; }; done
    export HARFBUZZ_CFLAGS="-I${HB}/usr/include/harfbuzz"
    export HARFBUZZ_LIBS="-L${HB}/usr/lib -lharfbuzz"
    LDFLAGS="${LDFLAGS} -Wl,-rpath-link,${HB}/usr/lib -Wl,-rpath-link,${GLIB}/usr/lib"
    HB_OPT=--with-harfbuzz
else
    HB_OPT=--without-harfbuzz
fi

substrate_libtool_fix "${TREE_DIR}/builds/unix/configure"
# FreeType drives builds/unix/configure from the top; run it in-tree.
cd "${TREE_DIR}"
make distclean >/dev/null 2>&1 || true
./configure \
    --host=i386-unknown-substrate \
    --prefix=/usr --libdir=/usr/lib --includedir=/usr/include \
    --enable-shared --enable-static \
    --with-zlib --with-png "${HB_OPT}" --without-brotli --without-bzip2 \
    CC=i386-unknown-substrate-gcc \
    AR=i386-unknown-substrate-ar RANLIB=i386-unknown-substrate-ranlib \
    CC_BUILD=gcc \
    CFLAGS="-march=i486 -mtune=i486 -O2 -g"
make -j"${JOBS}"
rm -rf "${DESTDIR}"; mkdir -p "${DESTDIR}"
make install DESTDIR="${DESTDIR}"
substrate_so_finalize "${DESTDIR}"
echo "==> freetype staged at ${DESTDIR}"
