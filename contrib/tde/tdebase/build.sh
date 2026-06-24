#!/bin/sh
# contrib/tde/tdebase/build.sh — cross-build tdebase (TDE Stage 5) for
# substrate.  WORK IN PROGRESS.
#
# Prerequisite ports: contrib/tde/{tqt3,tqtinterface,tde-cmake,
# dbus-1-tqt,tdelibs} plus the X11/freetype/fontconfig/jpeg/png stack in
# the cross sysroot, and the contrib/tde/tqt3 + tdelibs *host* tool
# builds (dcopidl2cpp / tdeconfig_compiler / maketdewidgets).
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
TDE_TOP="$(cd "${HERE}/.." && pwd)"
SUBSTRATE_TOP="$(cd "${HERE}/../../.." && pwd)"
: "${STAGE1_PREFIX:=/opt/substrate}"; : "${JOBS:=$(nproc 2>/dev/null || echo 4)}"
SR="${STAGE1_PREFIX}/i386-unknown-substrate"
VER="14.1.6"; TREE="${HERE}/build/tdebase-trinity-${VER}"
DEST="${SUBSTRATE_TOP}/dist-overlay/dist-tdebase"
MERGED="${SUBSTRATE_TOP}/dist-overlay/dist-tde-sysroot"
TDEROOT="${SUBSTRATE_TOP}/dist-overlay/tde-buildroot"
MODULES="${TDE_TOP}/tde-cmake/build/tde-cmake-trinity-${VER}/modules"
TC="${TDE_TOP}/substrate-tde-toolchain.cmake"
TQ="${MERGED}/opt/trinity"
HOSTBIN="${TDE_TOP}/tqt3/hostbuild/tqt-trinity-${VER}/bin"
[ -d "${TREE}" ] || { echo "run ./fetch.sh first" >&2; exit 1; }
[ -e "${TQ}/lib/libtdecore.so" ] || { echo "build tdelibs first (missing ${TQ}/lib/libtdecore.so)" >&2; exit 1; }
# Host code-generation tools are produced by contrib/tde/tdelibs/hostbuild.sh
# (run from tdelibs build.sh).  tde-config / meinproc-stub are tdebase-only.
for t in tde-config meinproc dcopidl2cpp tdeconfig_compiler maketdewidgets; do
    [ -x "${HOSTBIN}/${t}" ] || { echo "missing host tool ${HOSTBIN}/${t}; build tdelibs first (its hostbuild.sh produces them)" >&2; exit 1; }
done

PATH="${STAGE1_PREFIX}/bin:${HOSTBIN}:${PATH}"; export PATH

# tqt3integration's binding generator `gen` runs on the build host.  It
# uses only TQt (no tdecore), so compile it natively against the host
# TQt3 and point cmake at it via GEN_EXECUTABLE.
HBROOT="${TDE_TOP}/tqt3/hostbuild/tqt-trinity-${VER}"
g++ -O2 "${TREE}/tqt3integration/utils/gen.cpp" \
    -I"${HBROOT}/include/tqt" -I"${HBROOT}/include" -DTQT_THREAD_SUPPORT \
    -L"${HBROOT}/lib" -Wl,-rpath,"${HBROOT}/lib" -ltqt-mt -o "${HOSTBIN}/gen"

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
    -DGEN_EXECUTABLE="${HOSTBIN}/gen" \
    -DBUILD_ALL=ON -DBUILD_DOC=OFF -DBUILD_TSAK=OFF -DBUILD_TDEKBDLEDSYNC=OFF \
    -DWITH_ARTS=OFF -DWITH_ALSA=OFF -DWITH_CUPS=OFF -DWITH_XRANDR=OFF \
    -DWITH_XCOMPOSITE=OFF -DWITH_INOTIFY=OFF -DWITH_TDEHWLIB=OFF \
    -DWITH_XKB_TRANSLATIONS=OFF \
    -DWITH_LIBART=OFF -DWITH_LIBIDN=OFF -DWITH_GAMIN=OFF -DWITH_PCRE2=OFF -DWITH_SSL=OFF \
    -DHTDIG_SEARCH_BINARY=/usr/bin/htsearch \
    -DHAVE_NOGROUP_EXITCODE=1 -DHAVE_NOBODY_EXITCODE=0 \
    -DHONORS_SOCKET_PERMS_EXITCODE=1 -DCOVARIANT_RETURN_EXITCODE=0 \
    -DHAVE_GOOD_GETADDRINFO_EXITCODE=0 -DICEAUTH_PATH=/usr/bin/iceauth \
    -DINTLTOOL_MERGE_EXECUTABLE="${MODULES}/tde_l10n_merge.pl" \
    "${TREE}"
make -j"${JOBS}"
rm -rf "${DEST}"; make install DESTDIR="${DEST}"

# substrate: the ksmserver direct-launch workaround (tdebase patch 0006) starts
# kicker and kdesktop itself.  Their autostart .desktop entries would launch a
# SECOND copy, which TDEUniqueApplication defers via newInstance() (tdelibs
# patch 0009) — but kicker's newInstance reloads the panel, so the taskbar
# visibly loads twice.  Drop the redundant entries so the workaround is the
# sole launcher of the panel and desktop shell.
rm -f "${DEST}/opt/trinity/share/autostart/panel.desktop" \
      "${DEST}/opt/trinity/share/autostart/kdesktop.desktop"

_n=0
for so in $(find "${DEST}/opt/trinity" -name '*.so*' -type f); do
    printf '\100' | dd of="${so}" bs=1 seek=7 count=1 conv=notrunc status=none 2>/dev/null && _n=$((_n+1))
done
echo "  OSABI->substrate on ${_n} shared objects"

# Regenerate libtool ".la" stubs so TDE's KLibLoader::findLibrary() can
# resolve plugins by bare name (twin decorations, kded modules, kcm_*,
# ...).  See contrib/tde/tdelibs/build.sh for the rationale.
find "${DEST}" -name '*.la' -delete
sh "${SUBSTRATE_TOP}/contrib/tde/gen-libtool-la.sh" "${DEST}"

mkdir -p "${MERGED}/opt/trinity"
cp -a "${DEST}/opt/trinity/." "${MERGED}/opt/trinity/"
echo "==> tdebase staged under ${DEST}/opt/trinity and merged into ${MERGED}"
