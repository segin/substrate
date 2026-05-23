#!/bin/sh
# contrib/xorg-server/build.sh — cross-build Xfbdev for substrate.

set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
VERSION="1.16.4"
TREE_DIR="${HERE}/build/xorg-server-${VERSION}"
BUILD_DIR="${HERE}/build/build-stage-substrate"

if [ -z "${SUBSTRATE_TOP:-}" ]; then
    p="${HERE}"
    while [ "${p}" != "/" ] && [ ! -f "${p}/AGENTS.md" ] && [ ! -f "${p}/CLAUDE.md" ]; do
        p=$(dirname "${p}")
    done
    SUBSTRATE_TOP="${p}"
fi
: "${STAGE1_PREFIX:=/opt/substrate}"
: "${XORGPROTO_STAGE:=${SUBSTRATE_TOP}/dist-xorgproto}"
: "${XTRANS_STAGE:=${SUBSTRATE_TOP}/dist-xtrans}"
: "${LIBX11_STAGE:=${SUBSTRATE_TOP}/dist-libX11}"
: "${LIBXAU_STAGE:=${SUBSTRATE_TOP}/dist-libXau}"
: "${LIBXKBFILE_STAGE:=${SUBSTRATE_TOP}/dist-libxkbfile}"
: "${LIBFONTENC_STAGE:=${SUBSTRATE_TOP}/dist-libfontenc}"
: "${LIBXFONT_STAGE:=${SUBSTRATE_TOP}/dist-libXfont}"
: "${LIBXDMCP_STAGE:=${SUBSTRATE_TOP}/dist-libXdmcp}"
: "${LIBXSHMFENCE_STAGE:=${SUBSTRATE_TOP}/dist-libxshmfence}"
: "${PIXMAN_STAGE:=${SUBSTRATE_TOP}/dist-pixman}"
: "${DESTDIR:=${SUBSTRATE_TOP}/dist-xorg-server}"
: "${JOBS:=$(nproc 2>/dev/null || echo 4)}"

PATH="${STAGE1_PREFIX}/bin:${PATH}"
export PATH

[ -d "${TREE_DIR}" ] || { echo "build.sh: run ./fetch.sh first" >&2; exit 1; }

rm -rf "${BUILD_DIR}"
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

PKG_CONFIG_PATH=""
for d in "${XORGPROTO_STAGE}" "${XTRANS_STAGE}" "${LIBX11_STAGE}" "${LIBXAU_STAGE}" \
         "${LIBXKBFILE_STAGE}" "${LIBFONTENC_STAGE}" "${LIBXFONT_STAGE}" \
         "${LIBXDMCP_STAGE}" "${LIBXSHMFENCE_STAGE}" "${PIXMAN_STAGE}"; do
    [ -d "${d}/usr/lib/pkgconfig" ] && \
        PKG_CONFIG_PATH="${d}/usr/lib/pkgconfig${PKG_CONFIG_PATH:+:}${PKG_CONFIG_PATH}"
done
export PKG_CONFIG_PATH

CPPFLAGS=""
LDFLAGS=""
for d in "${XORGPROTO_STAGE}" "${XTRANS_STAGE}" "${LIBX11_STAGE}" "${LIBXAU_STAGE}" \
         "${LIBXKBFILE_STAGE}" "${LIBFONTENC_STAGE}" "${LIBXFONT_STAGE}" \
         "${LIBXDMCP_STAGE}" "${LIBXSHMFENCE_STAGE}" "${PIXMAN_STAGE}"; do
    [ -d "${d}/usr/include" ] && CPPFLAGS="-I${d}/usr/include ${CPPFLAGS}"
    [ -d "${d}/usr/lib" ]     && LDFLAGS="-L${d}/usr/lib ${LDFLAGS}"
done
export CPPFLAGS
export LDFLAGS="${LDFLAGS}-fno-pie"

echo "==> configure"
"${TREE_DIR}/configure" \
    --host=i386-unknown-substrate \
    --prefix=/usr \
    --libdir=/usr/lib \
    --datarootdir=/usr/share \
    --sysconfdir=/etc \
    --localstatedir=/var \
    --with-fontrootdir=/usr/share/fonts/X11 \
    --with-xkb-output=/var/lib/xkb \
    --with-xkb-path=/usr/share/X11/xkb \
    --enable-kdrive --enable-xfbdev \
    --disable-xorg --disable-xnest --disable-xephyr \
    --disable-xvfb --disable-xwin --disable-xquartz \
    --disable-dmx --disable-xwayland --disable-tslib \
    --disable-xevie \
    --disable-glamor --disable-dri --disable-dri2 --disable-dri3 \
    --disable-libdrm --disable-glx --disable-aiglx \
    --disable-strict-compilation --disable-libunwind --disable-config-udev --disable-config-hal --disable-systemd-logind \
    --disable-secure-rpc --disable-xshmfence \
    --disable-glx-tls \
    CC=i386-unknown-substrate-gcc \
    AR=i386-unknown-substrate-ar \
    RANLIB=i386-unknown-substrate-ranlib \
    CFLAGS="-march=i486 -mtune=i486 -O2 -g -fno-pie -w -fpermissive -Wno-incompatible-pointer-types -Wno-int-conversion -Wno-return-mismatch -Wno-array-bounds -Wno-stringop-overflow -Wno-stringop-truncation -Wno-restrict -Wno-deprecated-declarations -Wno-implicit-function-declaration"

echo "==> make -j${JOBS}"
make -j"${JOBS}"

echo "==> install into ${DESTDIR}"
rm -rf "${DESTDIR}"
mkdir -p "${DESTDIR}"
make install DESTDIR="${DESTDIR}"

echo "==> Done.  Xfbdev staged under ${DESTDIR}"
