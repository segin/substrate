#!/bin/sh
#
# contrib/cmake/build.sh — cross-build CMake as a Substrate ELF (on-VM cmake).
#
# Uses the HOST cmake to configure a cross build of the CMake sources with the
# substrate toolchain file, so the emitted cmake/ctest/cpack are substrate
# binaries.  All support libraries (curl, expat, zlib, bz2, zstd, libarchive,
# jsoncpp, librhash, libuv) are the bundled copies; libuv uses the generic
# poll(2) backend via the Substrate branch added by patch 0001 (substrate has
# no native epoll/kqueue).
#
# TLS (OpenSSL) in bundled curl is left OFF for this first port — an on-VM
# cmake without https downloads is still fully usable to configure/build local
# trees; it can be enabled later against the staged openssl.
#
# Env: STAGE1_PREFIX (default /opt/substrate), DESTDIR, JOBS.
set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
PKG="cmake"
VERSION="3.30.5"
TREE="${HERE}/build/cmake-${VERSION}"

if [ -z "${SUBSTRATE_TOP:-}" ]; then
    p="${HERE}"
    while [ "${p}" != "/" ] && [ ! -f "${p}/AGENTS.md" ] && [ ! -f "${p}/CLAUDE.md" ]; do p=$(dirname "${p}"); done
    SUBSTRATE_TOP="${p}"
fi
: "${STAGE1_PREFIX:=/opt/substrate}"
: "${DESTDIR:=${SUBSTRATE_TOP}/dist-overlay/dist-${PKG}}"
: "${JOBS:=$(nproc 2>/dev/null || echo 4)}"

export PATH="${STAGE1_PREFIX}/bin:${PATH}"
[ -d "${TREE}" ] || { echo "build.sh: run ./fetch.sh first" >&2; exit 1; }

BUILD="${TREE}/build-substrate"
rm -rf "${BUILD}"; mkdir -p "${BUILD}"; cd "${BUILD}"

cmake -S "${TREE}" -B "${BUILD}" \
    -DCMAKE_TOOLCHAIN_FILE="${HERE}/substrate.toolchain.cmake" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DBUILD_TESTING=OFF \
    -DCMAKE_USE_OPENSSL=OFF \
    -DBUILD_CursesDialog=OFF \
    -DBUILD_QtDialog=OFF \
    -DCMAKE_USE_SYSTEM_LIBRARIES=OFF

cmake --build "${BUILD}" -j"${JOBS}"

rm -rf "${DESTDIR}"
DESTDIR="${DESTDIR}" cmake --install "${BUILD}"

# Install the Platform/Substrate module set into the staged cmake's own module
# tree.  A native cmake running on the VM reports `uname -s == Substrate`, so
# without these it errors "System is unknown to cmake" on any native build.
# (The upstream install only ships CMake's stock Modules/Platform set.)
_platdir="$(ls -d "${DESTDIR}"/usr/share/cmake-*/Modules/Platform 2>/dev/null | head -1)"
if [ -n "${_platdir}" ]; then
    cp "${HERE}/cmake-modules/Platform/Substrate"*.cmake "${_platdir}/"
    echo "==> installed Platform/Substrate modules into ${_platdir}"
else
    echo "build.sh: WARNING: staged Modules/Platform dir not found" >&2
fi

# Brand every produced ELF ELFOSABI_SUBSTRATE(0x40) for ld.so dispatch.
"${HERE}/substrate-osabi-stamp.sh" "${DESTDIR}"

echo "==> ${PKG} ${VERSION} staged under ${DESTDIR}"
"${STAGE1_PREFIX}/bin/i386-unknown-substrate-readelf" -h "${DESTDIR}/usr/bin/cmake" 2>/dev/null | grep -iE "OS/ABI|Type|Machine" || true
