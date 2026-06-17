#!/bin/sh
# contrib/tde/tdelibs/build.sh — cross-build tdelibs (TDE Stage 4) for
# substrate.
#
# Prerequisite ports (build first): contrib/tde/{tqt3,tqtinterface,
# tde-cmake,dbus-1-tqt}, contrib/{file,libxml2,libxslt,dbus,glib},
# plus the X11/freetype/fontconfig/jpeg/png/zlib/bzip2 stack in the
# cross sysroot.  Also needs the contrib/tde/tqt3 *host* TQt3 build
# (hostbuild/), from which hostbuild.sh derives the native codegen tools.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
TDE_TOP="$(cd "${HERE}/.." && pwd)"
SUBSTRATE_TOP="$(cd "${HERE}/../../.." && pwd)"
: "${STAGE1_PREFIX:=/opt/substrate}"; : "${JOBS:=$(nproc 2>/dev/null || echo 4)}"
SR="${STAGE1_PREFIX}/i386-unknown-substrate"
VER="14.1.6"; TREE="${HERE}/build/tdelibs-trinity-${VER}"
DEST="${SUBSTRATE_TOP}/dist-overlay/dist-tdelibs"
MERGED="${SUBSTRATE_TOP}/dist-overlay/dist-tde-sysroot"
TDEROOT="${SUBSTRATE_TOP}/dist-overlay/tde-buildroot"
MODULES="${TDE_TOP}/tde-cmake/build/tde-cmake-trinity-${VER}/modules"
TC="${TDE_TOP}/substrate-tde-toolchain.cmake"
TQ="${MERGED}/opt/trinity"
HOSTBIN="${TDE_TOP}/tqt3/hostbuild/tqt-trinity-${VER}/bin"
[ -d "${TREE}" ] || { echo "run ./fetch.sh first" >&2; exit 1; }

# Build the native host code-generation tools (tdeconfig_compiler,
# maketdewidgets; dcopidl2cpp comes from the tqt3 host build) and put
# them ahead of the cross prefix on PATH.  A cross-built generator is a
# substrate binary that can't run on the build host.
"${HERE}/hostbuild.sh"
PATH="${STAGE1_PREFIX}/bin:${HOSTBIN}:${PATH}"; export PATH

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
    -DMAKETDEWIDGETS_EXECUTABLE="${HOSTBIN}/maketdewidgets" \
    -DWITH_ARTS=OFF -DWITH_ALSA=OFF -DWITH_CUPS=OFF -DWITH_XRANDR=OFF \
    -DWITH_XCOMPOSITE=OFF -DWITH_INOTIFY=OFF -DWITH_TDEHWLIB=OFF -DWITH_ISPELL=ON \
    -DWITH_LIBART=OFF -DWITH_LIBIDN=OFF -DWITH_GAMIN=OFF -DWITH_PCRE2=OFF -DWITH_SSL=OFF \
    -DHAVE_GOOD_GETADDRINFO_EXITCODE=0 -DICEAUTH_PATH=/usr/bin/iceauth \
    -DINTLTOOL_MERGE_EXECUTABLE="${MODULES}/tde_l10n_merge.pl" \
    "${TREE}"
make -j"${JOBS}"
rm -rf "${DEST}"; make install DESTDIR="${DEST}"

# Drop libtool archives and stamp ELFOSABI_SUBSTRATE (0x40) on every
# produced shared object — host `cc -shared` emits ELFOSABI_SYSV (0),
# which substrate's loader rejects.
find "${DEST}" -name '*.la' -delete
_n=0
for so in $(find "${DEST}/opt/trinity" -name '*.so*' -type f); do
    printf '\100' | dd of="${so}" bs=1 seek=7 count=1 conv=notrunc status=none 2>/dev/null && _n=$((_n+1))
done
echo "  OSABI->substrate on ${_n} shared objects"

# Merge into the shared TDE sysroot (tqt3 + tqtinterface + dbus-1-tqt +
# tdelibs) so downstream ports (tdebase, ...) resolve -ltdecore et al.
mkdir -p "${MERGED}/opt/trinity"
cp -a "${DEST}/opt/trinity/." "${MERGED}/opt/trinity/"

# tdelibs' install(EXPORT) bakes the nominal install prefix (/opt/trinity)
# into the imported-target IMPORTED_LOCATION paths.  When cross-compiling,
# the libraries physically live in the staged sysroot, so rewrite the
# export to point there; downstream (tdebase) links against real files
# while their own INSTALL_RPATH keeps the on-target /opt/trinity/lib.
_export="${MERGED}/opt/trinity/share/cmake/tdelibs.cmake"
[ -f "${_export}" ] && sed -i "s|\"/opt/trinity/|\"${MERGED}/opt/trinity/|g" "${_export}"
echo "==> tdelibs staged under ${DEST}/opt/trinity and merged into ${MERGED}"
