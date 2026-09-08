#!/bin/sh
# contrib/pango/build.sh — cross-compile Pango 1.42.4 for substrate.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
VERSION="1.42.4"; TREE_DIR="${HERE}/build/pango-${VERSION}"; BUILD_DIR="${HERE}/build/build-substrate"
if [ -z "${SUBSTRATE_TOP:-}" ]; then
    p="${HERE}"; while [ "${p}" != "/" ] && [ ! -f "${p}/AGENTS.md" ] && [ ! -f "${p}/CLAUDE.md" ]; do p=$(dirname "${p}"); done
    SUBSTRATE_TOP="${p}"
fi
: "${STAGE1_PREFIX:=/opt/substrate}"; : "${DESTDIR:=${SUBSTRATE_TOP}/dist-overlay/dist-pango}"
: "${JOBS:=$(nproc 2>/dev/null || echo 4)}"
PATH="${STAGE1_PREFIX}/bin:${PATH}"; export PATH
. "${HERE}/../substrate-autotools.sh"
[ -d "${TREE_DIR}" ] || { echo "build.sh: run ./fetch.sh first" >&2; exit 1; }

# The tarball's config.sub predates substrate and rejects the triple:
#     Invalid configuration `i386-unknown-substrate': OS `substrate' not recognized
# It works on a developer box only because the extracted tree there was
# patched by an earlier run.  substrate_config_sub_fix prefers the copy in
# the binutils port, patches the tree's own when that is absent (which is
# every CI run that hits the toolchain cache), and asserts the result.
substrate_config_sub_fix "${TREE_DIR}"

substrate_sysroot "${HERE}/build/sysroot" \
    glib2 libffi zlib freetype libpng expat fontconfig harfbuzz fribidi pixman cairo \
    xorgproto libXau xtrans libxcb libX11 libXext libXrender
export PYTHON=python3
export CFLAGS="-march=i486 -mtune=i486 -O2 -g -std=gnu11"
export CXXFLAGS="-march=i486 -mtune=i486 -O2 -g"

substrate_libtool_fix "${TREE_DIR}/configure"
rm -rf "${BUILD_DIR}"; mkdir -p "${BUILD_DIR}"; cd "${BUILD_DIR}"
"${TREE_DIR}/configure" \
    --host=i386-unknown-substrate \
    --prefix=/usr --libdir=/usr/lib --includedir=/usr/include \
    --enable-shared --enable-static \
    --with-cairo --disable-introspection --disable-gtk-doc --disable-installed-tests \
    CC=i386-unknown-substrate-gcc CXX=i386-unknown-substrate-g++ \
    AR=i386-unknown-substrate-ar RANLIB=i386-unknown-substrate-ranlib \
    CC_FOR_BUILD=gcc
make -j"${JOBS}"
rm -rf "${DESTDIR}"; mkdir -p "${DESTDIR}"
export PYTHONPATH="${HERE}/../automake-pyshim${PYTHONPATH:+:${PYTHONPATH}}"
make install DESTDIR="${DESTDIR}"
substrate_so_finalize "${DESTDIR}"
echo "==> pango staged at ${DESTDIR}"
