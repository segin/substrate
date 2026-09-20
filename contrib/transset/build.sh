#!/bin/sh
#
# contrib/transset/build.sh — cross-build transset for substrate.
#
# Set window transparency via _NET_WM_WINDOW_OPACITY.
#
# Env:
#   STAGE1_PREFIX   substrate toolchain prefix (default /opt/substrate)
#   DESTDIR         staging dir (default ${SUBSTRATE_TOP}/dist-overlay/dist-transset)
#   JOBS            parallel jobs (default `nproc`)

set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
LIB="transset"
VERSION="1.0.4"
TREE_DIR="${HERE}/build/transset-${VERSION}"
BUILD_DIR="${HERE}/build/build-stage-substrate"

if [ -z "${SUBSTRATE_TOP:-}" ]; then
    p="${HERE}"
    while [ "${p}" != "/" ] && [ ! -f "${p}/AGENTS.md" ] && [ ! -f "${p}/CLAUDE.md" ]; do
        p=$(dirname "${p}")
    done
    SUBSTRATE_TOP="${p}"
fi
: "${STAGE1_PREFIX:=/opt/substrate}"
: "${DESTDIR:=${SUBSTRATE_TOP}/dist-overlay/dist-transset}"
: "${JOBS:=$(nproc 2>/dev/null || echo 4)}"

PATH="${STAGE1_PREFIX}/bin:${PATH}"
export PATH

[ -d "${TREE_DIR}" ] || { echo "build.sh: run ./fetch.sh first" >&2; exit 1; }

# The tarball ships a config.sub that predates substrate and rejects the
# triple.  substrate_config_sub_fix prefers the copy in the binutils port
# and patches the tree's own when that is absent.
. "${HERE}/../substrate-autotools.sh"
substrate_config_sub_fix "${TREE_DIR}"

# Assemble dependency flags from every staged dist tree that exists.
PKGP=""; CPP=""; LDF=""
for d in xorgproto xcb-proto libXau xtrans libxcb libX11 libXext libXmu libICE libSM libXt libXaw libXpm libXrender libXft libXcursor libXfixes libXi libXtst libXinerama libXScrnSaver libxkbfile libfontenc libXfont2 xbitmaps libXrandr libXv libXcomposite libXdamage libXxf86vm libFS freetype fontconfig expat zlib libpng libiconv pixman xcb-util; do
    st="${SUBSTRATE_TOP}/dist-overlay/dist-${d}"
    [ -d "${st}/usr" ] || continue
    [ -d "${st}/usr/lib/pkgconfig" ] && PKGP="${PKGP}${PKGP:+:}${st}/usr/lib/pkgconfig"
    # Data-only packages (xbitmaps, the xorgproto .pc files) put theirs here.
    [ -d "${st}/usr/share/pkgconfig" ] && PKGP="${PKGP}${PKGP:+:}${st}/usr/share/pkgconfig"
    [ -d "${st}/usr/include" ] && CPP="${CPP} -I${st}/usr/include"
    [ -d "${st}/usr/lib" ] && LDF="${LDF} -L${st}/usr/lib -Wl,-rpath-link,${st}/usr/lib"
done
PKGP="${PKGP}:${SUBSTRATE_TOP}/contrib/libxcb/pkgconfig"

export PKG_CONFIG_LIBDIR="${PKGP}"
export CPPFLAGS="${CPP}"
export LDFLAGS="${LDF} -Wl,--copy-dt-needed-entries -l:libregex.so.0"

rm -rf "${BUILD_DIR}"
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

# configure carries its own copy of libtool's host cases, and an unknown
# host there means "this platform cannot do shared libraries" -- libtool
# then quietly builds only the static archive.  Teach it substrate.
substrate_libtool_fix "${TREE_DIR}/configure"

echo "==> configure"
"${TREE_DIR}/configure" \
    --host=i386-unknown-substrate \
    --prefix=/usr \
    --disable-specs \
    --disable-docs \
    --without-xmlto \
    --without-fop \
    --without-xsltproc \
    xorg_cv_malloc0_returns_null=no \
    CC=i386-unknown-substrate-gcc \
    AR=i386-unknown-substrate-ar \
    RANLIB=i386-unknown-substrate-ranlib \
    CC_FOR_BUILD=gcc \
    CFLAGS="-march=i486 -mtune=i486 -O2 -g -fno-pie"

echo "==> make -j${JOBS}"
make -j"${JOBS}"

echo "==> install into ${DESTDIR}"
rm -rf "${DESTDIR}"
mkdir -p "${DESTDIR}"
make install DESTDIR="${DESTDIR}"

echo "==> Done.  ${LIB} staged under ${DESTDIR}"
