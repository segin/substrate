#!/bin/sh
# contrib/tde/tdelibs/build.sh — cross-build tdelibs (TDE Stage 4) for
# substrate.  WORK IN PROGRESS: configures and compiles; still iterating
# through substrate-libc/POSIX gaps in the build.
#
# Prerequisite ports (build first): contrib/tde/{tqt3,tqtinterface,
# tde-cmake,dbus-1-tqt}, contrib/{file,libxml2,libxslt,dbus,glib},
# plus the X11/freetype/fontconfig/jpeg/png/zlib/bzip2 stack in the
# cross sysroot.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
TDE_TOP="$(cd "${HERE}/.." && pwd)"
SUBSTRATE_TOP="$(cd "${HERE}/../../.." && pwd)"
: "${STAGE1_PREFIX:=/opt/substrate}"; : "${JOBS:=$(nproc 2>/dev/null || echo 4)}"
PATH="${STAGE1_PREFIX}/bin:${PATH}"; export PATH
SR="${STAGE1_PREFIX}/i386-unknown-substrate"
VER="14.1.6"; TREE="${HERE}/build/tdelibs-trinity-${VER}"
DEST="${SUBSTRATE_TOP}/dist-overlay/dist-tdelibs"
MERGED="${SUBSTRATE_TOP}/dist-overlay/dist-tde-sysroot"
TDEROOT="${SUBSTRATE_TOP}/dist-overlay/tde-buildroot"
MODULES="${TDE_TOP}/tde-cmake/build/tde-cmake-trinity-${VER}/modules"
TC="${TDE_TOP}/substrate-tde-toolchain.cmake"
TQ="${MERGED}/opt/trinity"
[ -d "${TREE}" ] || { echo "run ./fetch.sh first" >&2; exit 1; }

# The X .pc Requires chain (xrender -> x11 -> xcb -> pthread-stubs) needs
# the pthread-stubs stub .pc in the sysroot.
cp -f "${SUBSTRATE_TOP}/contrib/libxcb/pkgconfig/"*.pc "${SR}/lib/pkgconfig/" 2>/dev/null || true

# Unified build-root: one PKG_CONFIG_SYSROOT_DIR that resolves both the
# /usr-prefixed cross-sysroot .pc files and the /opt/trinity-prefixed TQt
# ones, via symlinks.
rm -rf "${TDEROOT}"; mkdir -p "${TDEROOT}/opt"
ln -s "${SR}" "${TDEROOT}/usr"
ln -s "${MERGED}/opt/trinity" "${TDEROOT}/opt/trinity"
export PKG_CONFIG_SYSROOT_DIR="${TDEROOT}"
export PKG_CONFIG_LIBDIR="${TDEROOT}/usr/lib/pkgconfig:${TDEROOT}/opt/trinity/lib/pkgconfig"

cd "${TREE}"; rm -rf obj; mkdir obj; cd obj
cmake -G "Unix Makefiles" \
    -DCMAKE_TOOLCHAIN_FILE="${TC}" -DCMAKE_MODULE_PATH="${MODULES}" \
    -DCMAKE_INSTALL_PREFIX=/opt/trinity -DQT_PREFIX_DIR="${TQ}" \
    -DMOC_EXECUTABLE="${TQ}/bin/tqmoc" -DUIC_EXECUTABLE="${TQ}/bin/tquic" \
    -DWITH_ARTS=OFF -DWITH_ALSA=OFF -DWITH_CUPS=OFF -DWITH_XRANDR=OFF \
    -DWITH_XCOMPOSITE=OFF -DWITH_INOTIFY=OFF -DWITH_TDEHWLIB=OFF -DWITH_ISPELL=ON \
    -DWITH_LIBART=OFF -DWITH_LIBIDN=OFF -DWITH_GAMIN=OFF -DWITH_PCRE2=OFF -DWITH_SSL=OFF \
    -DHAVE_GOOD_GETADDRINFO_EXITCODE=0 -DICEAUTH_PATH=/usr/bin/iceauth \
    -DINTLTOOL_MERGE_EXECUTABLE="${MODULES}/tde_l10n_merge.pl" \
    "${TREE}"
make -j"${JOBS}"
rm -rf "${DEST}"; make install DESTDIR="${DEST}"
echo "==> tdelibs staged under ${DEST}/opt/trinity"
