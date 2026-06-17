#!/bin/sh
# contrib/tde/tqtinterface/build.sh — cross-compile tqtinterface (TDE
# Stage 2) for substrate.
#
# CMake project using the TDE shared cmake modules (contrib/tde/tde-cmake)
# and the substrate TDE cross toolchain.  Finds the cross TQt3 built by
# contrib/tde/tqt3 via QT_PREFIX_DIR, and uses TQt3's host tqmoc/tquic.
set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
TDE_TOP="$(cd "${HERE}/.." && pwd)"
SUBSTRATE_TOP="$(cd "${HERE}/../../.." && pwd)"
: "${STAGE1_PREFIX:=/opt/substrate}"
: "${JOBS:=$(nproc 2>/dev/null || echo 4)}"
VERSION="14.1.6"
TREE="${HERE}/build/tqtinterface-trinity-${VERSION}"
OBJ="${HERE}/build/obj"
: "${DESTDIR:=${SUBSTRATE_TOP}/dist-overlay/dist-tqtinterface}"

PATH="${STAGE1_PREFIX}/bin:${PATH}"; export PATH
[ -d "${TREE}" ] || { echo "build.sh: run ./fetch.sh first" >&2; exit 1; }

# Cross TQt3 (built + staged by contrib/tde/tqt3).
TQT="${SUBSTRATE_TOP}/dist-overlay/dist-tqt3/opt/trinity"
[ -d "${TQT}/include" ] || { echo "build.sh: build contrib/tde/tqt3 first (missing ${TQT})" >&2; exit 1; }

# TDE shared cmake modules.
MODULES="${TDE_TOP}/tde-cmake/build/tde-cmake-trinity-${VERSION}/modules"
[ -d "${MODULES}" ] || { echo "build.sh: run ../tde-cmake/fetch.sh first" >&2; exit 1; }

TOOLCHAIN="${TDE_TOP}/substrate-tde-toolchain.cmake"

# tqt-mt.pc for the OpenGL/inputmethod qt_config probe.
export PKG_CONFIG_PATH="${TQT}/lib/pkgconfig"

echo "==> cmake configure"
rm -rf "${OBJ}"; mkdir -p "${OBJ}"; cd "${OBJ}"
cmake -G "Unix Makefiles" \
    -DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN}" \
    -DCMAKE_MODULE_PATH="${MODULES}" \
    -DCMAKE_INSTALL_PREFIX=/opt/trinity \
    -DCMAKE_BUILD_TYPE=Release \
    -DQT_PREFIX_DIR="${TQT}" \
    -DMOC_EXECUTABLE="${TQT}/bin/tqmoc" \
    -DUIC_EXECUTABLE="${TQT}/bin/tquic" \
    "${TREE}"

echo "==> make -j${JOBS}"
make -j"${JOBS}"

echo "==> install into ${DESTDIR}"
rm -rf "${DESTDIR}"
make install DESTDIR="${DESTDIR}"

# Stamp ELFOSABI_SUBSTRATE on the produced shared objects.
find "${DESTDIR}" -name '*.la' -delete
_n=0
for so in $(find "${DESTDIR}/opt/trinity" -name '*.so*' -type f); do
    printf '\100' | dd of="${so}" bs=1 seek=7 count=1 conv=notrunc status=none 2>/dev/null && _n=$((_n+1))
done
echo "  OSABI->substrate on ${_n} shared objects"
echo "==> Done.  tqtinterface staged under ${DESTDIR}/opt/trinity"
