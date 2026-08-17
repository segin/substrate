#!/bin/sh
#
# contrib/sdl2-compat/build.sh — cross-build sdl2-compat for substrate.
#
# sdl2-compat provides the SDL2 ABI (libSDL2-2.0.so.0) implemented on top of
# SDL3, so existing SDL2 consumers keep linking against the same soname while
# the actual work happens in SDL3.  It replaces the old contrib/sdl2 port.
#
# It is a pure API shim: no substrate-specific source patches are needed.  It
# needs SDL3's *headers* at build time (find_package(SDL3 COMPONENTS Headers))
# and dlopens libSDL3.so.0 at run time, so contrib/sdl3 must be built and
# staged first.
#
# Env: STAGE1_PREFIX (default /opt/substrate), DESTDIR, JOBS.

set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
LIB="sdl2-compat"
VERSION="2.32.70"
TREE_DIR="${HERE}/build/sdl2-compat-${VERSION}"
BUILD_DIR="${HERE}/build/build-stage-substrate"
X11ROOT="${HERE}/build/x11root"

if [ -z "${SUBSTRATE_TOP:-}" ]; then
    p="${HERE}"
    while [ "${p}" != "/" ] && [ ! -f "${p}/AGENTS.md" ] && [ ! -f "${p}/CLAUDE.md" ]; do
        p=$(dirname "${p}")
    done
    SUBSTRATE_TOP="${p}"
fi
: "${STAGE1_PREFIX:=/opt/substrate}"
: "${DESTDIR:=${SUBSTRATE_TOP}/dist-overlay/dist-${LIB}}"
: "${JOBS:=$(nproc 2>/dev/null || echo 4)}"
: "${SUBSTRATE_SYSROOT:=${STAGE1_PREFIX}/i386-unknown-substrate}"

PATH="${STAGE1_PREFIX}/bin:${PATH}"
export PATH

[ -d "${TREE_DIR}" ] || { echo "build.sh: run ./fetch.sh first" >&2; exit 1; }

SDL3_DIST="${SUBSTRATE_TOP}/dist-overlay/dist-sdl3"
[ -d "${SDL3_DIST}/usr/include/SDL3" ] || {
    echo "build.sh: SDL3 not staged — build contrib/sdl3 first" >&2
    exit 1
}

# --- mini-sysroot ----------------------------------------------------------
# SDL2COMPAT_X11=ON does find_package(X11 REQUIRED), so the X client trees
# have to be visible; merge them together with the staged SDL3 tree.
rm -rf "${X11ROOT}"; mkdir -p "${X11ROOT}/usr"
_have=0
for d in xorgproto xtrans libXau libxcb libX11 libXext \
         libXcursor libXi libXfixes libXrender libXScrnSaver libiconv sdl3; do
    st="${SUBSTRATE_TOP}/dist-overlay/dist-${d}"
    [ -d "${st}/usr" ] || continue
    cp -a "${st}/usr/." "${X11ROOT}/usr/"
    _have=$((_have + 1))
done
[ "${_have}" -ge 13 ] || {
    echo "build.sh: only ${_have}/13 dist trees found — build the X chain + sdl3 first" >&2
    exit 1
}

export PKG_CONFIG_LIBDIR="${X11ROOT}/usr/lib/pkgconfig:${X11ROOT}/usr/share/pkgconfig"

rm -rf "${BUILD_DIR}"; mkdir -p "${BUILD_DIR}"; cd "${BUILD_DIR}"

echo "==> cmake configure"
cmake "${TREE_DIR}" \
    -DCMAKE_TOOLCHAIN_FILE="${SUBSTRATE_TOP}/contrib/cmake/substrate.toolchain.cmake" \
    -DCMAKE_FIND_ROOT_PATH="${SUBSTRATE_SYSROOT};${X11ROOT}/usr" \
    -DCMAKE_PREFIX_PATH="${X11ROOT}/usr" \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_FLAGS="-march=i486 -mtune=i486 -I${X11ROOT}/usr/include" \
    -DCMAKE_SHARED_LINKER_FLAGS="-L${X11ROOT}/usr/lib -Wl,-rpath-link,${X11ROOT}/usr/lib -l:libc.so.0 -l:libsys.so.0" \
    -DSDL3_INCLUDE_DIRS="${X11ROOT}/usr/include" \
    -DSDL2COMPAT_STATIC=ON \
    -DSDL2COMPAT_X11=ON \
    -DSDL2COMPAT_TESTS=OFF \
    -DSDL2COMPAT_INSTALL=ON \
    -DSDL2COMPAT_INSTALL_CPACK=OFF \
    ${CONFIGURE_EXTRA:-}

echo "==> make -j${JOBS}"
make -j"${JOBS}"

echo "==> install into ${DESTDIR}"
rm -rf "${DESTDIR}"; mkdir -p "${DESTDIR}"
make install DESTDIR="${DESTDIR}"

sh "${SUBSTRATE_TOP}/contrib/cmake/substrate-osabi-stamp.sh" "${DESTDIR}"

# Upstream installs the pkg-config file as sdl2-compat.pc, so that it can sit
# beside a real SDL2 without colliding.  On substrate there is no other SDL2 —
# this port IS SDL2 — and every consumer asks pkg-config for "sdl2" (PsyMP3
# does).  Its contents are already the correct SDL2 answer; only the filename
# differs, so publish it under that name too.  Without this the SDL2 the ports
# actually link would be invisible to pkg-config.
_pcdir="${DESTDIR}/usr/lib/pkgconfig"
if [ -f "${_pcdir}/sdl2-compat.pc" ]; then
    cp -a "${_pcdir}/sdl2-compat.pc" "${_pcdir}/sdl2.pc"
fi

echo "==> Done.  ${LIB} staged under ${DESTDIR}"
