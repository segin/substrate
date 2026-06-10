#!/bin/sh
# contrib/cairo/build.sh — cross-compile cairo 1.16.0 for substrate.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
VERSION="1.16.0"; TREE_DIR="${HERE}/build/cairo-${VERSION}"; BUILD_DIR="${HERE}/build/build-substrate"
if [ -z "${SUBSTRATE_TOP:-}" ]; then
    p="${HERE}"; while [ "${p}" != "/" ] && [ ! -f "${p}/AGENTS.md" ] && [ ! -f "${p}/CLAUDE.md" ]; do p=$(dirname "${p}"); done
    SUBSTRATE_TOP="${p}"
fi
: "${STAGE1_PREFIX:=/opt/substrate}"; : "${DESTDIR:=${SUBSTRATE_TOP}/dist-cairo}"
: "${JOBS:=$(nproc 2>/dev/null || echo 4)}"
PATH="${STAGE1_PREFIX}/bin:${PATH}"; export PATH
. "${HERE}/../substrate-autotools.sh"
[ -d "${TREE_DIR}" ] || { echo "build.sh: run ./fetch.sh first" >&2; exit 1; }

# Assemble flags from all prerequisite dist trees.
CPP=""; LDF=""; PKGP=""
for d in zlib libpng pixman freetype expat fontconfig \
         xorgproto libXau xtrans libxcb libX11 libXext libXrender; do
    st="${SUBSTRATE_TOP}/dist-${d}"
    [ -d "${st}/usr" ] || { echo "build.sh: dist-${d} missing — build contrib/${d} first" >&2; exit 1; }
    CPP="${CPP} -I${st}/usr/include"
    LDF="${LDF} -L${st}/usr/lib -Wl,-rpath-link,${st}/usr/lib"
    [ -d "${st}/usr/lib/pkgconfig" ] && PKGP="${PKGP}${PKGP:+:}${st}/usr/lib/pkgconfig"
done
PKGP="${PKGP}:${SUBSTRATE_TOP}/contrib/libxcb/pkgconfig"
export CPPFLAGS="${CPP} -I${SUBSTRATE_TOP}/dist-freetype/usr/include/freetype2"
export LDFLAGS="${LDF} -Wl,--copy-dt-needed-entries"
export PKG_CONFIG_LIBDIR="${PKGP}"
# cross run-tests + substrate malloc(0)
export ac_cv_func_malloc_0_nonnull=yes ac_cv_func_realloc_0_nonnull=yes
export ax_cv_c_float_words_bigendian=no
# substrate gcc has no -pthread flag; force cairo to probe with -lpthread.
export pthread_CFLAGS="" pthread_LIBS="-lpthread"

substrate_libtool_fix "${TREE_DIR}/configure"
rm -rf "${BUILD_DIR}"; mkdir -p "${BUILD_DIR}"; cd "${BUILD_DIR}"
"${TREE_DIR}/configure" \
    --host=i386-unknown-substrate \
    --prefix=/usr --libdir=/usr/lib --includedir=/usr/include \
    --enable-shared --enable-static \
    --enable-xlib --enable-xlib-xrender --enable-ft --enable-fc --enable-png \
    --disable-xcb --disable-gl --disable-gobject --disable-script \
    --disable-interpreter --disable-trace --disable-valgrind \
    --disable-ps --disable-pdf --disable-svg \
    CC=i386-unknown-substrate-gcc \
    AR=i386-unknown-substrate-ar RANLIB=i386-unknown-substrate-ranlib \
    CC_FOR_BUILD=gcc \
    CFLAGS="-march=i486 -mtune=i486 -O2 -g"
# Build only src (the library + cairo*.pc).  test/ has a pdiff.h that
# typedefs bool, which gcc16's C23 rejects; doc/util/boilerplate/perf are
# not runtime.
make -j"${JOBS}" SUBDIRS=src
rm -rf "${DESTDIR}"; mkdir -p "${DESTDIR}"
make install SUBDIRS=src DESTDIR="${DESTDIR}"
substrate_so_finalize "${DESTDIR}"
echo "==> cairo staged at ${DESTDIR}"
