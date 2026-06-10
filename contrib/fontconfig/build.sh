#!/bin/sh
# contrib/fontconfig/build.sh — cross-compile fontconfig 2.14.2 for substrate.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
VERSION="2.14.2"; TREE_DIR="${HERE}/build/fontconfig-${VERSION}"; BUILD_DIR="${HERE}/build/build-substrate"
if [ -z "${SUBSTRATE_TOP:-}" ]; then
    p="${HERE}"; while [ "${p}" != "/" ] && [ ! -f "${p}/AGENTS.md" ] && [ ! -f "${p}/CLAUDE.md" ]; do p=$(dirname "${p}"); done
    SUBSTRATE_TOP="${p}"
fi
: "${STAGE1_PREFIX:=/opt/substrate}"; : "${DESTDIR:=${SUBSTRATE_TOP}/dist-fontconfig}"
: "${JOBS:=$(nproc 2>/dev/null || echo 4)}"
PATH="${STAGE1_PREFIX}/bin:${PATH}"; export PATH
. "${HERE}/../substrate-autotools.sh"
[ -d "${TREE_DIR}" ] || { echo "build.sh: run ./fetch.sh first" >&2; exit 1; }
FT="${SUBSTRATE_TOP}/dist-freetype"; XP="${SUBSTRATE_TOP}/dist-expat"; ZL="${SUBSTRATE_TOP}/dist-zlib"; PNG="${SUBSTRATE_TOP}/dist-libpng"
for d in "${FT}" "${XP}"; do [ -d "${d}/usr" ] || { echo "build.sh: ${d} missing" >&2; exit 1; }; done

export CPPFLAGS="-I${FT}/usr/include -I${FT}/usr/include/freetype2 -I${XP}/usr/include -I${ZL}/usr/include -I${PNG}/usr/include"
export LDFLAGS="-L${FT}/usr/lib -L${XP}/usr/lib -L${ZL}/usr/lib -L${PNG}/usr/lib -Wl,-rpath-link,${FT}/usr/lib -Wl,-rpath-link,${XP}/usr/lib -Wl,-rpath-link,${ZL}/usr/lib -Wl,-rpath-link,${PNG}/usr/lib -Wl,--copy-dt-needed-entries"
export PKG_CONFIG_LIBDIR="${FT}/usr/lib/pkgconfig:${XP}/usr/lib/pkgconfig:${ZL}/usr/lib/pkgconfig:${PNG}/usr/lib/pkgconfig"
export FREETYPE_CFLAGS="-I${FT}/usr/include/freetype2"
export FREETYPE_LIBS="-L${FT}/usr/lib -lfreetype"
export EXPAT_CFLAGS="-I${XP}/usr/include"
export EXPAT_LIBS="-L${XP}/usr/lib -lexpat"
export ac_cv_func_malloc_0_nonnull=yes ac_cv_func_realloc_0_nonnull=yes

substrate_libtool_fix "${TREE_DIR}/configure"
rm -rf "${BUILD_DIR}"; mkdir -p "${BUILD_DIR}"; cd "${BUILD_DIR}"
"${TREE_DIR}/configure" \
    --host=i386-unknown-substrate \
    --prefix=/usr --libdir=/usr/lib --includedir=/usr/include --sysconfdir=/etc \
    --localstatedir=/var --with-default-fonts=/usr/share/fonts \
    --enable-shared --enable-static \
    --disable-docs --with-expat="${XP}/usr" \
    CC=i386-unknown-substrate-gcc \
    AR=i386-unknown-substrate-ar RANLIB=i386-unknown-substrate-ranlib \
    CC_FOR_BUILD=gcc \
    CFLAGS="-march=i486 -mtune=i486 -O2 -g"
make -j"${JOBS}"
rm -rf "${DESTDIR}"; mkdir -p "${DESTDIR}"
make install DESTDIR="${DESTDIR}"
substrate_so_finalize "${DESTDIR}"
echo "==> fontconfig staged at ${DESTDIR}"
