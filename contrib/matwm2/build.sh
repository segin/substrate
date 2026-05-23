#!/bin/sh
# contrib/matwm2/build.sh — cross-build matwm2 for substrate.
#
# matwm2 is a lightweight overlapping X11 window manager.  Optional
# upstream features (shape, xinerama, xft) all require deps we don't
# have on substrate yet (libXinerama, libXft + freetype) — build with
# everything optional disabled, just libX11.  Adds substrate's first
# real WM, runs on top of the kdrive Xfbdev port.

set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
COMMIT="master"
TREE_DIR="${HERE}/build/matwm2-${COMMIT}"

if [ -z "${SUBSTRATE_TOP:-}" ]; then
    p="${HERE}"
    while [ "${p}" != "/" ] && [ ! -f "${p}/AGENTS.md" ] && [ ! -f "${p}/CLAUDE.md" ]; do
        p=$(dirname "${p}")
    done
    SUBSTRATE_TOP="${p}"
fi
: "${STAGE1_PREFIX:=/opt/substrate}"
: "${XORGPROTO_STAGE:=${SUBSTRATE_TOP}/dist-xorgproto}"
: "${LIBX11_STAGE:=${SUBSTRATE_TOP}/dist-libX11}"
: "${LIBXAU_STAGE:=${SUBSTRATE_TOP}/dist-libXau}"
: "${LIBXCB_STAGE:=${SUBSTRATE_TOP}/dist-libxcb}"
: "${DESTDIR:=${SUBSTRATE_TOP}/dist-matwm2}"

PATH="${STAGE1_PREFIX}/bin:${PATH}"
export PATH

[ -d "${TREE_DIR}" ] || { echo "build.sh: run ./fetch.sh first" >&2; exit 1; }

cd "${TREE_DIR}"

# Upstream's hand-rolled configure does its own toolchain probing with
# library / header search across a hardcoded list of prefixes.  Wire
# it at our cross sysroot + the per-package dist trees.
SUBSTRATE_INCS=""
SUBSTRATE_LIBS=""
for d in "${XORGPROTO_STAGE}" "${LIBX11_STAGE}" "${LIBXAU_STAGE}" "${LIBXCB_STAGE}"; do
    [ -d "${d}/usr/include" ] && SUBSTRATE_INCS="${SUBSTRATE_INCS} -I${d}/usr/include"
    [ -d "${d}/usr/lib" ]     && SUBSTRATE_LIBS="${SUBSTRATE_LIBS} -L${d}/usr/lib"
done

echo "==> configure"
# --force-x11 because matwm2's library probe doesn't know about the
# substrate cross prefix; force-mark x11 found and let our explicit
# -I / -L flags do the wiring at compile.  Disable everything else
# (shape / xinerama / xft) — we have no libXinerama / libXft.
CC=i386-unknown-substrate-gcc \
CFLAGS="-std=gnu99 -march=i486 -mtune=i486 -O2 -g -fno-pie ${SUBSTRATE_INCS}" \
LDFLAGS="-fno-pie ${SUBSTRATE_LIBS}" \
./configure \
    --prefix=/usr \
    --disable-pkg-conf \
    --disable-shape --disable-xinerama --disable-xft --disable-vfork \
    --force-x11 \
    --cflags="${SUBSTRATE_INCS}" \
    --ldflags="${SUBSTRATE_LIBS}" \
    --libs="-lX11 -lxcb -lXau -lsys"

echo "==> make"
make CC=i386-unknown-substrate-gcc \
     CFLAGS="-std=gnu99 -march=i486 -mtune=i486 -O2 -g -fno-pie ${SUBSTRATE_INCS}" \
     LDFLAGS="-fno-pie ${SUBSTRATE_LIBS}"

echo "==> install into ${DESTDIR}"
rm -rf "${DESTDIR}"
mkdir -p "${DESTDIR}/usr/bin"
cp matwm2 "${DESTDIR}/usr/bin/matwm2"
# Also ship the default rc file under /etc — matwm2 reads ~/.matwmrc
# or /etc/matwmrc.
mkdir -p "${DESTDIR}/etc"
cp default_matwmrc "${DESTDIR}/etc/matwmrc"

echo "==> Done.  matwm2 staged under ${DESTDIR}"
