#!/bin/sh
# contrib/tde/tdetoys/build.sh — cross-build a curated subset of tdetoys
# (TDE applications) for substrate.
#
# Prerequisite ports: contrib/tde/{tqt3,tqtinterface,tde-cmake,dbus-1-tqt,
# tdelibs} plus the X11/freetype/fontconfig/jpeg/png stack in the cross
# sysroot, and the tqt3 + tdelibs *host* tool builds (tqmoc/tquic/
# dcopidl2cpp/tdeconfig_compiler/maketdewidgets/tde-config/meinproc).
#
# Only the apps that build against tdelibs alone are enabled here — kcalc,
# kcharselect, khexedit, kjots, ktimer, kdf.  The rest (kgpg→gpgme,
# ksim→lm_sensors, superkaramba→python, klaptopdaemon/kmilo→laptop HW,
# tdewallet→crypto, ark→libarchive wiring, kregexpeditor→kmultiform) are
# left OFF for now; flip their BUILD_* flags on as their deps land.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
TDE_TOP="$(cd "${HERE}/.." && pwd)"
SUBSTRATE_TOP="$(cd "${HERE}/../../.." && pwd)"
: "${STAGE1_PREFIX:=/opt/substrate}"; : "${JOBS:=$(nproc 2>/dev/null || echo 4)}"
SR="${STAGE1_PREFIX}/i386-unknown-substrate"
VER="14.1.6"; TREE="${HERE}/build/tdetoys-trinity-${VER}"
DEST="${SUBSTRATE_TOP}/dist-overlay/dist-tdetoys"
MERGED="${SUBSTRATE_TOP}/dist-overlay/dist-tde-sysroot"
TDEROOT="${SUBSTRATE_TOP}/dist-overlay/tde-buildroot"
MODULES="${TDE_TOP}/tde-cmake/build/tde-cmake-trinity-${VER}/modules"
TC="${TDE_TOP}/substrate-tde-toolchain.cmake"
TQ="${MERGED}/opt/trinity"
HOSTBIN="${TDE_TOP}/tqt3/hostbuild/tqt-trinity-${VER}/bin"
[ -d "${TREE}" ] || { echo "run ./fetch.sh first" >&2; exit 1; }
[ -e "${TQ}/lib/libtdecore.so" ] || { echo "build tdelibs first (missing ${TQ}/lib/libtdecore.so)" >&2; exit 1; }
for t in tde-config meinproc dcopidl2cpp tdeconfig_compiler maketdewidgets; do
    [ -x "${HOSTBIN}/${t}" ] || { echo "missing host tool ${HOSTBIN}/${t}; build tdelibs first" >&2; exit 1; }
done

PATH="${STAGE1_PREFIX}/bin:${HOSTBIN}:${PATH}"; export PATH

# pthread-stubs stub .pc for the X .pc Requires chain.
cp -f "${SUBSTRATE_TOP}/contrib/libxcb/pkgconfig/"*.pc "${SR}/lib/pkgconfig/" 2>/dev/null || true

# Unified build-root resolving both /usr- and /opt/trinity-prefixed .pc.
rm -rf "${TDEROOT}"; mkdir -p "${TDEROOT}/opt"
ln -s "${SR}" "${TDEROOT}/usr"
ln -s "${MERGED}/opt/trinity" "${TDEROOT}/opt/trinity"
export PKG_CONFIG_SYSROOT_DIR="${TDEROOT}"
export PKG_CONFIG_LIBDIR="${TDEROOT}/usr/lib/pkgconfig:${TDEROOT}/opt/trinity/lib/pkgconfig"

cd "${TREE}"; rm -rf obj; mkdir obj; cd obj
cmake -G "Unix Makefiles" \
    -DCMAKE_TOOLCHAIN_FILE="${TC}" -DCMAKE_MODULE_PATH="${MODULES}" \
    -DCMAKE_INSTALL_PREFIX=/opt/trinity -DQT_PREFIX_DIR="${TQ}" \
    -DTDE_PREFIX="${TQ}" \
    -DMOC_EXECUTABLE="${TQ}/bin/tqmoc" -DUIC_EXECUTABLE="${TQ}/bin/tquic" \
    -DKDECONFIG_EXECUTABLE="${HOSTBIN}/tde-config" \
    -DKDE3_DCOPIDL_EXECUTABLE="${TQ}/bin/dcopidl" \
    -DKDE3_DCOPIDLNG_EXECUTABLE="${TQ}/bin/dcopidlng" \
    -DKDE3_DCOPIDL2CPP_EXECUTABLE="${HOSTBIN}/dcopidl2cpp" \
    -DKDE3_MEINPROC_EXECUTABLE="${HOSTBIN}/meinproc" \
    -DKDE3_KCFGC_EXECUTABLE="${HOSTBIN}/tdeconfig_compiler" \
    -DKDE3_MAKETDEWIDGETS_EXECUTABLE="${HOSTBIN}/maketdewidgets" \
    -DMAKETDEWIDGETS_EXECUTABLE="${HOSTBIN}/maketdewidgets" \
    -DBUILD_ALL=OFF -DBUILD_DOC=OFF \
    -DBUILD_KMOON=ON -DBUILD_KWORLDWATCH=ON -DBUILD_KTEATIME=ON -DBUILD_AMOR=ON \
    -DWITH_T1LIB=OFF -DWITH_ARTS=OFF \
    -DINTLTOOL_MERGE_EXECUTABLE="${MODULES}/tde_l10n_merge.pl" \
    "${TREE}"
make -j"${JOBS}"
rm -rf "${DEST}"; make install DESTDIR="${DEST}"

_n=0
for so in $(find "${DEST}/opt/trinity" -name '*.so*' -type f); do
    printf '\100' | dd of="${so}" bs=1 seek=7 count=1 conv=notrunc status=none 2>/dev/null && _n=$((_n+1))
done
echo "  OSABI->substrate on ${_n} shared objects"

# Regenerate libtool ".la" stubs so KLibLoader::findLibrary() can resolve
# plugins by bare name (see contrib/tde/tdelibs/build.sh).
find "${DEST}" -name '*.la' -delete
sh "${SUBSTRATE_TOP}/contrib/tde/gen-libtool-la.sh" "${DEST}"

mkdir -p "${MERGED}/opt/trinity"
cp -a "${DEST}/opt/trinity/." "${MERGED}/opt/trinity/"
echo "==> tdetoys staged under ${DEST}/opt/trinity and merged into ${MERGED}"
