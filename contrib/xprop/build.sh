#!/bin/sh
#
# contrib/xprop/build.sh — cross-build xprop for substrate.
# xprop displays and sets window/font properties on an X server: it
# dumps the properties of a window (or the root window) and is used by
# scripts (e.g. TDE's starttde) to probe the root window for an existing
# session.  Produces /usr/bin/xprop.
#
# Depends only on contrib/{xorgproto,libX11}.

set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
VERSION="1.2.8"
TREE_DIR="${HERE}/build/xprop-${VERSION}"
BUILD_DIR="${HERE}/build/build-stage-substrate"

if [ -z "${SUBSTRATE_TOP:-}" ]; then
    p="${HERE}"
    while [ "${p}" != "/" ] && [ ! -f "${p}/AGENTS.md" ] && [ ! -f "${p}/CLAUDE.md" ]; do
        p=$(dirname "${p}")
    done
    SUBSTRATE_TOP="${p}"
fi
: "${STAGE1_PREFIX:=/opt/substrate}"
: "${DESTDIR:=${SUBSTRATE_TOP}/dist-overlay/dist-xprop}"
: "${JOBS:=$(nproc 2>/dev/null || echo 4)}"

PATH="${STAGE1_PREFIX}/bin:${PATH}"; export PATH
[ -d "${TREE_DIR}" ] || { echo "build.sh: run ./fetch.sh first" >&2; exit 1; }

PKGP=""; CPP=""; LDF=""
for d in xorgproto xcb-proto libXau xtrans libxcb libX11; do
    st="${SUBSTRATE_TOP}/dist-overlay/dist-${d}"
    [ -d "${st}/usr" ] || continue
    [ -d "${st}/usr/lib/pkgconfig" ] && PKGP="${PKGP}${PKGP:+:}${st}/usr/lib/pkgconfig"
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

echo "==> Done.  xprop staged under ${DESTDIR}"
