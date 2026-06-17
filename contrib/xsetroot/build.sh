#!/bin/sh
#
# contrib/xsetroot/build.sh — cross-build xsetroot for substrate.
# xsetroot sets X root-window attributes: background (solid colour,
# bitmap, or gray weave), pointer cursor (-cursor / -cursor_name), and
# can reset to defaults.  Produces /usr/bin/xsetroot.
#
# Depends on contrib/{xorgproto,libX11,libXmu(xmuu),libXrender,libXfixes,
# libXcursor,xbitmaps} staged first.

set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
VERSION="1.1.3"
TREE_DIR="${HERE}/build/xsetroot-${VERSION}"
BUILD_DIR="${HERE}/build/build-stage-substrate"

if [ -z "${SUBSTRATE_TOP:-}" ]; then
    p="${HERE}"
    while [ "${p}" != "/" ] && [ ! -f "${p}/AGENTS.md" ] && [ ! -f "${p}/CLAUDE.md" ]; do
        p=$(dirname "${p}")
    done
    SUBSTRATE_TOP="${p}"
fi
: "${STAGE1_PREFIX:=/opt/substrate}"
: "${DESTDIR:=${SUBSTRATE_TOP}/dist-overlay/dist-xsetroot}"
: "${JOBS:=$(nproc 2>/dev/null || echo 4)}"

PATH="${STAGE1_PREFIX}/bin:${PATH}"; export PATH
[ -d "${TREE_DIR}" ] || { echo "build.sh: run ./fetch.sh first" >&2; exit 1; }

# Assemble dependency flags from every staged X dist tree.  Some packages
# (xbitmaps) install their .pc under share/pkgconfig, so add both dirs.
PKGP=""; CPP=""; LDF=""
for d in xorgproto xcb-proto libXau xtrans libxcb libX11 libXext libXmu \
         libXrender libXfixes libXcursor xbitmaps; do
    st="${SUBSTRATE_TOP}/dist-overlay/dist-${d}"
    [ -d "${st}/usr" ] || continue
    [ -d "${st}/usr/lib/pkgconfig" ]   && PKGP="${PKGP}${PKGP:+:}${st}/usr/lib/pkgconfig"
    [ -d "${st}/usr/share/pkgconfig" ] && PKGP="${PKGP}${PKGP:+:}${st}/usr/share/pkgconfig"
    [ -d "${st}/usr/include" ] && CPP="${CPP} -I${st}/usr/include"
    [ -d "${st}/usr/lib" ] && LDF="${LDF} -L${st}/usr/lib -Wl,-rpath-link,${st}/usr/lib"
done
PKGP="${PKGP}:${SUBSTRATE_TOP}/contrib/libxcb/pkgconfig"

export PKG_CONFIG_LIBDIR="${PKGP}"
export CPPFLAGS="${CPP}"
export LDFLAGS="${LDF} -Wl,--copy-dt-needed-entries"

rm -rf "${BUILD_DIR}"; mkdir -p "${BUILD_DIR}"; cd "${BUILD_DIR}"

echo "==> configure"
"${TREE_DIR}/configure" \
    --host=i386-unknown-substrate \
    --prefix=/usr \
    CC=i386-unknown-substrate-gcc \
    AR=i386-unknown-substrate-ar \
    RANLIB=i386-unknown-substrate-ranlib \
    CFLAGS="-march=i486 -mtune=i486 -O2 -g -fno-pie"

echo "==> make -j${JOBS}"
make -j"${JOBS}"

echo "==> install into ${DESTDIR}"
rm -rf "${DESTDIR}"; mkdir -p "${DESTDIR}"
make install DESTDIR="${DESTDIR}"

echo "==> Done.  xsetroot staged under ${DESTDIR}"
