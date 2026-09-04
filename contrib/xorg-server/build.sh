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
: "${DESTDIR:=${SUBSTRATE_TOP}/dist-overlay/dist-xorg-server}"
: "${JOBS:=$(nproc 2>/dev/null || echo 4)}"

PATH="${STAGE1_PREFIX}/bin:${PATH}"
export PATH

[ -d "${TREE_DIR}" ] || { echo "build.sh: run ./fetch.sh first" >&2; exit 1; }

rm -rf "${BUILD_DIR}"
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

# Resolve dependencies from the cross sysroot, where every port before this
# one has been installed (build.sh syncs after each).  libX11 is built on
# XCB, so x11.pc requires xcb.pc, which requires xau.pc and pthread-stubs.pc;
# the sysroot carries the whole set plus the headers, so no -I/-L
# enumeration of dist trees is needed.
#
# LIBDIR rather than PATH: PATH only ADDS to pkg-config's defaults, which do
# not include the sysroot, so a PATH listing a few dist trees resolved the
# rest out of the build host's /usr/lib/pkgconfig -- feeding host flags into
# a cross build wherever the host had X installed, and failing outright
# where it did not.
export PKG_CONFIG_LIBDIR="${STAGE1_PREFIX}/i386-unknown-substrate/lib/pkgconfig"

# pixman's header lives in a SUBDIRECTORY of includedir, and pixman-1.pc says
# "Cflags: -I${includedir}/pixman-1" with includedir=/usr/include -- a TARGET
# path.  pkg-config therefore hands the build -I/usr/include/pixman-1, which
# on a machine with host pixman installed silently compiles the cross build
# against the host's headers, and on one without it fails outright:
#
#   include/miscstruct.h:52:10: fatal error: pixman.h: No such file or directory
#
# Name the sysroot's copy.  Overriding PIXMAN_CFLAGS is not enough: what
# reaches the compile line is XSERVERCFLAGS_CFLAGS, which re-queries pixman-1
# as part of REQUIRED_LIBS.  Every other dependency here is content with the
# compiler's own sysroot search; pixman is the only one with a subdirectory.
export CPPFLAGS="-I${STAGE1_PREFIX}/i386-unknown-substrate/include/pixman-1"

# -lfontenc: libXfont references it but records no DT_NEEDED of its own.
export LDFLAGS="-lfontenc -fno-pie"

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
