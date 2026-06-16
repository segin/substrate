#!/bin/sh
# contrib/ctwm/build.sh — cross-build ctwm for substrate.
#
# ctwm is twm extended with virtual screens, workspaces, EWMH, and XPM
# icons.  CMake build via a substrate cross toolchain file; the staged X
# dist trees (incl. libXpm) are merged into one mini-sysroot that CMake's
# FindX11 searches.  Optional deps we don't have are turned off:
#   USE_JPEG=OFF  (no libjpeg)   USE_XRANDR=OFF (no libXrandr)
#   USE_M4=OFF    (no m4 on target; .ctwmrc is parsed without preprocessing)
# XPM stays on (libXpm is ported); EWMH / XSMP (libSM/libICE) stay on.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
VERSION="4.1.0"
TREE_DIR="${HERE}/build/ctwm-${VERSION}"
BUILD_DIR="${HERE}/build/build-stage-substrate"
export X11ROOT="${HERE}/build/x11root"
if [ -z "${SUBSTRATE_TOP:-}" ]; then
    p="${HERE}"
    while [ "${p}" != "/" ] && [ ! -f "${p}/AGENTS.md" ] && [ ! -f "${p}/CLAUDE.md" ]; do
        p=$(dirname "${p}"); done
    SUBSTRATE_TOP="${p}"
fi
: "${STAGE1_PREFIX:=/opt/substrate}"
: "${DESTDIR:=${SUBSTRATE_TOP}/dist-overlay/dist-ctwm}"
: "${JOBS:=$(nproc 2>/dev/null || echo 4)}"
PATH="${STAGE1_PREFIX}/bin:${PATH}"; export PATH
[ -d "${TREE_DIR}" ] || { echo "build.sh: run ./fetch.sh first" >&2; exit 1; }

# Merge the staged X dist trees (incl. libXpm) into one mini-sysroot.
rm -rf "${X11ROOT}"; mkdir -p "${X11ROOT}/usr"
_have=0
for d in xorgproto libXau xtrans libxcb libX11 libXext libICE libSM libXt libXmu libXpm; do
    st="${SUBSTRATE_TOP}/dist-overlay/dist-${d}"
    [ -d "${st}/usr" ] || continue
    cp -a "${st}/usr/." "${X11ROOT}/usr/"
    _have=$((_have + 1))
done
[ "${_have}" -ge 10 ] || { echo "build.sh: only ${_have}/11 X dist trees found — build the X chain (incl. libXpm) first" >&2; exit 1; }

# Stage libregex into the sysroot: ctwm's USE_SREGEX needs POSIX regcomp/
# regexec, which on substrate live in libregex (not libc).
for rx in "${SUBSTRATE_TOP}/usr.lib/regex/libregex.so.0" "${SUBSTRATE_TOP}/dist/usr/lib/libregex.so.0"; do
    if [ -f "${rx}" ]; then
        cp -a "${rx}" "${X11ROOT}/usr/lib/"
        ln -sf libregex.so.0 "${X11ROOT}/usr/lib/libregex.so"
        break
    fi
done
[ -f "${X11ROOT}/usr/lib/libregex.so.0" ] || { echo "build.sh: libregex.so.0 not found — build usr.lib/regex first" >&2; exit 1; }

rm -rf "${BUILD_DIR}"; mkdir -p "${BUILD_DIR}"; cd "${BUILD_DIR}"

echo "==> cmake configure"
cmake "${TREE_DIR}" \
    -DCMAKE_TOOLCHAIN_FILE="${HERE}/substrate-toolchain.cmake" \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DCMAKE_BUILD_TYPE=Release \
    -DUSE_JPEG=OFF \
    -DUSE_XRANDR=OFF \
    -DUSE_M4=OFF \
    -DUSE_XPM=ON \
    -DUSE_EWMH=ON \
    -DUSE_SESSION=ON \
    -DHAS_REGEX_H=1 \
    -DHAS_REGEXEC=1

echo "==> make -j${JOBS}"
make -j"${JOBS}"

echo "==> install into ${DESTDIR}"
rm -rf "${DESTDIR}"; mkdir -p "${DESTDIR}"
make install DESTDIR="${DESTDIR}"
echo "==> Done.  ctwm staged at ${DESTDIR}/usr/bin/ctwm"
