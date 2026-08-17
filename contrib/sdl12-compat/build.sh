#!/bin/sh
#
# contrib/sdl12-compat/build.sh — cross-build sdl12-compat for substrate.
#
# sdl12-compat provides the SDL 1.2 ABI (libSDL-1.2.so.0) implemented on top
# of SDL2.  On substrate that SDL2 is itself sdl2-compat, which is in turn
# implemented on SDL3 — so the full stack at run time is
#
#     app -> libSDL-1.2.so.0 -> libSDL2-2.0.so.0 -> libSDL3.so.0 -> X11 / audio
#
# It needs SDL2's *headers* at build time and dlopens libSDL2-2.0.so.0 at run
# time, so contrib/sdl3 and contrib/sdl2-compat must be built and staged first.
#
# Env: STAGE1_PREFIX (default /opt/substrate), DESTDIR, JOBS.

set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
LIB="sdl12-compat"
VERSION="1.2.76"
TREE_DIR="${HERE}/build/sdl12-compat-${VERSION}"
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

SDL2_DIST="${SUBSTRATE_TOP}/dist-overlay/dist-sdl2-compat"
[ -d "${SDL2_DIST}/usr/include/SDL2" ] || {
    echo "build.sh: sdl2-compat not staged — build contrib/sdl2-compat first" >&2
    exit 1
}

# --- mini-sysroot ----------------------------------------------------------
rm -rf "${X11ROOT}"; mkdir -p "${X11ROOT}/usr"
_have=0
for d in xorgproto xtrans libXau libxcb libX11 libXext \
         libXcursor libXi libXfixes libXrender libXScrnSaver libiconv \
         sdl3 sdl2-compat; do
    st="${SUBSTRATE_TOP}/dist-overlay/dist-${d}"
    [ -d "${st}/usr" ] || continue
    cp -a "${st}/usr/." "${X11ROOT}/usr/"
    _have=$((_have + 1))
done
[ "${_have}" -ge 14 ] || {
    echo "build.sh: only ${_have}/14 dist trees found — build the X chain, sdl3 and sdl2-compat first" >&2
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
    -DSDL2_INCLUDE_DIR="${X11ROOT}/usr/include/SDL2" \
    -DSDL2_INCLUDE_DIRS="${X11ROOT}/usr/include/SDL2" \
    -DSDL12TESTS=OFF \
    -DSDL12DEVEL=ON \
    -DSTATICDEVEL=ON \
    ${CONFIGURE_EXTRA:-}

echo "==> make -j${JOBS}"
make -j"${JOBS}"

echo "==> install into ${DESTDIR}"
rm -rf "${DESTDIR}"; mkdir -p "${DESTDIR}"
make install DESTDIR="${DESTDIR}"

sh "${SUBSTRATE_TOP}/contrib/cmake/substrate-osabi-stamp.sh" "${DESTDIR}"

echo "==> Done.  ${LIB} staged under ${DESTDIR}"
