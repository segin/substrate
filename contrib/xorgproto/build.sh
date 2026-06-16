#!/bin/sh
#
# contrib/xorgproto/build.sh — install the X.Org protocol headers
# for substrate.  Produces (header-only, no compiled objects):
#   /usr/include/X11/*.h               core protocol headers
#   /usr/include/X11/extensions/*.h    extension protocol headers
#   /usr/include/GL/*.h                GLX protocol headers
#   /usr/lib/pkgconfig/*.pc            xproto / xextproto / ... metadata
#
# xorgproto is the foundation of the X11 client stack: libXau,
# libxcb and libX11 all need its headers (X.h, Xproto.h,
# keysymdef.h, the extension protocol definitions).
#
# Env:
#   STAGE1_PREFIX   substrate toolchain prefix (default /opt/substrate)
#   DESTDIR         staging dir (default ${SUBSTRATE_TOP}/dist-overlay/dist-xorgproto)
#   JOBS            parallel jobs (default `nproc`)

set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
VERSION="2024.1"
TREE_DIR="${HERE}/build/xorgproto-${VERSION}"
BUILD_DIR="${HERE}/build/build-stage-substrate"

if [ -z "${SUBSTRATE_TOP:-}" ]; then
    p="${HERE}"
    while [ "${p}" != "/" ] && [ ! -f "${p}/AGENTS.md" ] && [ ! -f "${p}/CLAUDE.md" ]; do
        p=$(dirname "${p}")
    done
    SUBSTRATE_TOP="${p}"
fi
: "${STAGE1_PREFIX:=/opt/substrate}"
: "${DESTDIR:=${SUBSTRATE_TOP}/dist-overlay/dist-xorgproto}"
: "${JOBS:=$(nproc 2>/dev/null || echo 4)}"

PATH="${STAGE1_PREFIX}/bin:${PATH}"
export PATH

[ -d "${TREE_DIR}" ] || { echo "build.sh: run ./fetch.sh first" >&2; exit 1; }

rm -rf "${BUILD_DIR}"
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

echo "==> configure"
"${TREE_DIR}/configure" \
    --host=i386-unknown-substrate \
    --prefix=/usr \
    --libdir=/usr/lib \
    --includedir=/usr/include \
    CC=i386-unknown-substrate-gcc

echo "==> make -j${JOBS}"
make -j"${JOBS}"

echo "==> install into ${DESTDIR}"
rm -rf "${DESTDIR}"
mkdir -p "${DESTDIR}"
make install DESTDIR="${DESTDIR}"

# xorgproto installs its .pc files to $(datadir)/pkgconfig
# (/usr/share/pkgconfig).  build.sh's sync_to_sysroot only mirrors
# usr/lib and usr/include, so relocate the .pc files into
# usr/lib/pkgconfig where the rest of the X11 chain looks for them.
if [ -d "${DESTDIR}/usr/share/pkgconfig" ]; then
    mkdir -p "${DESTDIR}/usr/lib/pkgconfig"
    mv "${DESTDIR}"/usr/share/pkgconfig/*.pc "${DESTDIR}/usr/lib/pkgconfig/" 2>/dev/null || true
    rmdir "${DESTDIR}/usr/share/pkgconfig" 2>/dev/null || true
fi

echo "==> Done.  xorgproto headers staged under ${DESTDIR}"
