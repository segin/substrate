#!/bin/sh
# contrib/libxkbfile/build.sh — cross-build libxkbfile for substrate.
# Produces /usr/lib/libxkbfile.{a,so.1} + headers + pkgconfig.
#
# libxkbfile is a shared-memory fence primitive used by X clients
# and the X server for cross-process synchronization (DRI3 sync,
# present extension, etc).  Required dependency of xorg-server.

set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
VERSION="1.1.3"
TREE_DIR="${HERE}/build/libxkbfile-${VERSION}"
BUILD_DIR="${HERE}/build/build-stage-substrate"

if [ -z "${SUBSTRATE_TOP:-}" ]; then
    p="${HERE}"
    while [ "${p}" != "/" ] && [ ! -f "${p}/AGENTS.md" ] && [ ! -f "${p}/CLAUDE.md" ]; do
        p=$(dirname "${p}")
    done
    SUBSTRATE_TOP="${p}"
fi
: "${STAGE1_PREFIX:=/opt/substrate}"
: "${XORGPROTO_STAGE:=${SUBSTRATE_TOP}/dist-overlay/dist-xorgproto}"
: "${LIBX11_STAGE:=${SUBSTRATE_TOP}/dist-overlay/dist-libX11}"
: "${DESTDIR:=${SUBSTRATE_TOP}/dist-overlay/dist-libxkbfile}"
: "${JOBS:=$(nproc 2>/dev/null || echo 4)}"

PATH="${STAGE1_PREFIX}/bin:${PATH}"
export PATH

[ -d "${TREE_DIR}" ] || { echo "build.sh: run ./fetch.sh first" >&2; exit 1; }

rm -rf "${BUILD_DIR}"
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

# Assemble dependency flags from every staged X dist tree -- the same
# pattern libXt/libXext/libXaw use.  Listing the full set is harmless: a
# library does not reference the dirs it has no use for.
#
# xorgproto + libX11 alone is not enough, even though those are the only
# two this library actually uses.  x11.pc carries "Requires.private: xcb",
# xcb.pc carries "Requires.private: xau pthread-stubs", and pkg-config
# resolves that chain before it will answer for x11 at all:
#
#   configure: error: Package requirements (x11 kbproto) were not met:
#   Package 'xcb', required by 'x11', not found
#
# PKG_CONFIG_LIBDIR rather than PKG_CONFIG_PATH, because PATH *adds to* the
# default search dirs: on a build host with X development packages
# installed the chain silently resolved through /usr/lib/pkgconfig, which
# both hid this bug and fed host flags into a cross build.
PKGP=""; CPP=""; LDF=""
for d in xorgproto xcb-proto libXau libXdmcp xtrans libxcb libX11; do
    st="${SUBSTRATE_TOP}/dist-overlay/dist-${d}"
    [ -d "${st}/usr" ] || continue
    [ -d "${st}/usr/lib/pkgconfig" ] && PKGP="${PKGP}${PKGP:+:}${st}/usr/lib/pkgconfig"
    [ -d "${st}/usr/include" ] && CPP="${CPP} -I${st}/usr/include"
    [ -d "${st}/usr/lib" ] && LDF="${LDF} -L${st}/usr/lib -Wl,-rpath-link,${st}/usr/lib"
done
# pthread-stubs.pc has no port of its own -- substrate's libpthread provides
# what it stands for -- so contrib/libxcb ships a hand-written one.
PKGP="${PKGP}:${SUBSTRATE_TOP}/contrib/libxcb/pkgconfig"

export PKG_CONFIG_LIBDIR="${PKGP}"
export CPPFLAGS="${CPP}"
export LDFLAGS="${LDF} -fno-pie -Wl,--copy-dt-needed-entries"

echo "==> configure"
"${TREE_DIR}/configure" \
    --host=i386-unknown-substrate \
    --prefix=/usr \
    --libdir=/usr/lib \
    --enable-shared \
    --enable-static \
    CC=i386-unknown-substrate-gcc \
    AR=i386-unknown-substrate-ar \
    RANLIB=i386-unknown-substrate-ranlib \
    CFLAGS="-march=i486 -mtune=i486 -O2 -g -fno-pie"

echo "==> make -j${JOBS}"
make -j"${JOBS}"

echo "==> install into ${DESTDIR}"
rm -rf "${DESTDIR}"
mkdir -p "${DESTDIR}"
make install DESTDIR="${DESTDIR}"

rm -f "${DESTDIR}"/usr/lib/*.la

for so in "${DESTDIR}"/usr/lib/*.so.*; do
    [ -f "${so}" ] || continue
    [ -L "${so}" ] && continue
    printf '\100' | dd of="${so}" bs=1 seek=7 count=1 conv=notrunc status=none
    echo "  OSABI->substrate on $(basename "${so}")"
done

echo "==> Done.  libxkbfile staged under ${DESTDIR}"
