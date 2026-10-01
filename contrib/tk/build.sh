#!/bin/sh
#
# contrib/tk/build.sh — cross-build Tk 8.6 for substrate.
#
# Builds libtk8.6.a, wish, the headers, the script library and tkConfig.sh
# into dist-tk.  Static, like contrib/tcl: Tcl is built --disable-shared, so
# a shared Tk would have no shared Tcl to load against; wish links both
# archives and the X libraries.
#
# Tk is configured against Tcl's BUILD tree, not its staged copy.  The
# staged tclConfig.sh describes the target's /usr (TCL_LIB_SPEC
# '-L/usr/lib'), which on the build host names the host's own libraries;
# pointed at the build tree, Tk's configure uses TCL_BUILD_LIB_SPEC and
# TCL_SRC_DIR instead.  So contrib/tcl must have been built first, and its
# build/ directory kept.
#
# Fonts go through Xft (freetype + fontconfig), Tk's default, and the idle
# timer through the XScreenSaver extension; both libraries are ported.
#
# Env:
#   STAGE1_PREFIX   substrate toolchain prefix (default /opt/substrate)
#   DESTDIR         staging dir (default ${SUBSTRATE_TOP}/dist-overlay/dist-tk)
#   JOBS            parallel jobs (default `nproc`)

set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
VERSION="8.6.16"
TREE_DIR="${HERE}/build/tk${VERSION}"
BUILD_DIR="${HERE}/build/build-stage-substrate"

if [ -z "${SUBSTRATE_TOP:-}" ]; then
    p="${HERE}"
    while [ "${p}" != "/" ] && [ ! -f "${p}/AGENTS.md" ] && [ ! -f "${p}/CLAUDE.md" ]; do
        p=$(dirname "${p}")
    done
    SUBSTRATE_TOP="${p}"
fi
: "${STAGE1_PREFIX:=/opt/substrate}"
: "${DESTDIR:=${SUBSTRATE_TOP}/dist-overlay/dist-tk}"
: "${JOBS:=$(nproc 2>/dev/null || echo 4)}"
PATH="${STAGE1_PREFIX}/bin:${PATH}"; export PATH

TCL_UNIX="${SUBSTRATE_TOP}/contrib/tcl/build/tcl${VERSION}/unix"

[ -d "${TREE_DIR}" ] || { echo "build.sh: run ./fetch.sh first" >&2; exit 1; }
[ -f "${TCL_UNIX}/tclConfig.sh" ] && [ -f "${TCL_UNIX}/libtcl8.6.a" ] || {
    echo "build.sh: build contrib/tcl first (needs ${TCL_UNIX}/tclConfig.sh" \
         "and libtcl8.6.a)" >&2; exit 1; }

# Assemble dependency flags from every staged dist tree that exists.
PKGP=""; CPP=""; LDF=""; XLIB=""
for d in xorgproto xcb-proto libXau libXdmcp xtrans libxcb libX11 libXext \
         libXrender libXft libXScrnSaver freetype fontconfig expat zlib \
         libpng bzip2; do
    st="${SUBSTRATE_TOP}/dist-overlay/dist-${d}"
    [ -d "${st}/usr" ] || continue
    [ -d "${st}/usr/lib/pkgconfig" ] && PKGP="${PKGP}${PKGP:+:}${st}/usr/lib/pkgconfig"
    [ -d "${st}/usr/share/pkgconfig" ] && PKGP="${PKGP}${PKGP:+:}${st}/usr/share/pkgconfig"
    [ -d "${st}/usr/include" ] && CPP="${CPP} -I${st}/usr/include"
    [ -d "${st}/usr/lib" ] && LDF="${LDF} -L${st}/usr/lib -Wl,-rpath-link,${st}/usr/lib"
done
for d in libX11 libXft libXScrnSaver fontconfig freetype; do
    [ -d "${SUBSTRATE_TOP}/dist-overlay/dist-${d}/usr" ] || {
        echo "build.sh: dist-${d} not staged; build contrib/${d} first" >&2; exit 1; }
done
XLIB="${SUBSTRATE_TOP}/dist-overlay/dist-libX11/usr"
PKGP="${PKGP}:${SUBSTRATE_TOP}/contrib/libxcb/pkgconfig"

export PKG_CONFIG_LIBDIR="${PKGP}"
export CPPFLAGS="${CPP}"
export LDFLAGS="${LDF} -Wl,--copy-dt-needed-entries"

rm -rf "${BUILD_DIR}"
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

echo "==> configure"
"${TREE_DIR}/unix/configure" \
    --host=i386-unknown-substrate \
    --prefix=/usr \
    --disable-shared \
    --with-tcl="${TCL_UNIX}" \
    --x-includes="${XLIB}/include" \
    --x-libraries="${XLIB}/lib" \
    CC=i386-unknown-substrate-gcc \
    AR=i386-unknown-substrate-ar \
    RANLIB=i386-unknown-substrate-ranlib \
    CFLAGS="-march=i486 -mtune=i486 -O2 -g -fPIC"

echo "==> make -j${JOBS}"
make -j"${JOBS}"

echo "==> install into ${DESTDIR}"
rm -rf "${DESTDIR}"
make install-binaries install-libraries install-headers install-private-headers \
    DESTDIR="${DESTDIR}"

if [ -f "${DESTDIR}/usr/bin/wish8.6" ]; then
    ln -sf wish8.6 "${DESTDIR}/usr/bin/wish"
fi

echo "==> Done.  Tk ${VERSION} staged under ${DESTDIR}"
