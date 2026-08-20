#!/bin/sh
# contrib/tde/dbus-1-tqt/build.sh — cross-build the TQt D-Bus binding.
# Needs: contrib/dbus (libdbus-1) + contrib/tde/{tqt3,tqtinterface,tde-cmake}.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
TDE_TOP="$(cd "${HERE}/.." && pwd)"
SUBSTRATE_TOP="$(cd "${HERE}/../../.." && pwd)"
: "${STAGE1_PREFIX:=/opt/substrate}"; : "${JOBS:=$(nproc 2>/dev/null || echo 4)}"
PATH="${STAGE1_PREFIX}/bin:${PATH}"; export PATH
VER="14.1.6"; TREE="${HERE}/build/dbus-1-tqt-trinity-${VER}"
DEST="${SUBSTRATE_TOP}/dist-overlay/dist-dbus-1-tqt"
MERGED="${SUBSTRATE_TOP}/dist-overlay/dist-tde-sysroot"
TDEROOT="${SUBSTRATE_TOP}/dist-overlay/tde-buildroot"
TQ="${MERGED}/opt/trinity"
MODULES="${TDE_TOP}/tde-cmake/build/tde-cmake-trinity-${VER}/modules"
TC="${TDE_TOP}/substrate-tde-toolchain.cmake"
[ -d "${TREE}" ] || { echo "run ./fetch.sh first" >&2; exit 1; }

# Assemble the merged sysroot and the pkg-config build-root this configure
# reads.  This script used only to *reference* ${TDEROOT}, while tdelibs was
# the only thing that built it -- so dbus-1-tqt could only configure if
# tdelibs had already been built, even though it comes BEFORE tdelibs in the
# dependency order.  From a clean tree it failed with "dbus-1 is required, but
# was not found" despite dbus being installed correctly, because the build-root
# the pkg-config paths pointed into did not exist at all.
"${TDE_TOP}/merge-staging.sh"

export PKG_CONFIG_SYSROOT_DIR="${TDEROOT}"
export PKG_CONFIG_LIBDIR="${TDEROOT}/usr/lib/pkgconfig:${TDEROOT}/opt/trinity/lib/pkgconfig"
cd "${TREE}"; rm -rf obj; mkdir obj; cd obj
cmake -G "Unix Makefiles" \
    -DCMAKE_TOOLCHAIN_FILE="${TC}" -DCMAKE_MODULE_PATH="${MODULES}" \
    -DCMAKE_INSTALL_PREFIX=/opt/trinity -DQT_PREFIX_DIR="${TQ}" \
    -DMOC_EXECUTABLE="${TQ}/bin/tqmoc" -DUIC_EXECUTABLE="${TQ}/bin/tquic" \
    "${TREE}"
make -j"${JOBS}"
rm -rf "${DEST}"; make install DESTDIR="${DEST}"
find "${DEST}" -name '*.la' -delete
for so in $(find "${DEST}/opt/trinity" -name '*.so*' -type f); do
    printf '\100' | dd of="${so}" bs=1 seek=7 count=1 conv=notrunc status=none
done
# merge into the shared TDE sysroot so tdelibs' FindTQt/dbus checks resolve it
cp -a "${DEST}/opt/trinity/." "${MERGED}/opt/trinity/"
echo "==> dbus-1-tqt staged under ${DEST}/opt/trinity"
